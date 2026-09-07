/**
 * Tracraft Dash Round — точка входа.
 *
 * Поток кадра:
 *   1. Signals — наполняются симуляцией (sim.enabled в лейауте) или,
 *      в будущем, CAN и внешними датчиками.
 *   2. Render::screen — рисует активный экран в фреймбуфер PSRAM.
 *   3. Display::flush — один блит на панель по QSPI.
 *
 * Рендер выбран вариант B из docs/RENDER_AB.md: свой рендерер на Arduino_GFX.
 */

#include <Arduino.h>

#include "burn_in_protection.h"
#include "display_hw.h"
#include "layout_store.h"
#include "pins.h"
#include "signal_bus.h"
#include "sim_signals.h"
#include "text_render.h"
#include "touch_hw.h"
#include "widget_render.h"

namespace {

uint32_t s_bootMs   = 0;
uint8_t  s_screen   = 0;
bool     s_simOn    = false;
bool     s_haveLayout = false;

// Профилировка кадра. Отрисовка и отправка замеряются раздельно: только так
// видно, во что упираемся — в собственный рендер или в пропускную способность
// QSPI. Теоретический потолок отправки — около 21 мс на кадр (424 КБ, 40 МГц,
// QIO), всё что сверх этого лежит в рендере.
uint32_t s_frames      = 0;
uint32_t s_lastFpsMs   = 0;
uint32_t s_drawSumUs   = 0;
uint32_t s_drawMaxUs   = 0;
uint32_t s_flushSumUs  = 0;
uint32_t s_flushMaxUs  = 0;

/// Автопереключение экранов, если тач не поднялся: без него устройство
/// показывало бы только первый экран и выглядело мёртвым.
constexpr uint32_t kScreenDwellMs = 8000;
uint32_t s_lastSwitchMs = 0;

// ─── Перелистывание свайпом ──────────────────────────────────────────────────
//
// Состояние жеста. Идея та же, что на телефоне: пока палец на экране, оба
// экрана двигаются ровно за ним; после отпускания смещение доезжает само —
// либо до следующего экрана, либо назад, в зависимости от пройденного пути.

/// Ниже этого сдвига считаем, что палец просто дрожит, а не листает.
constexpr int   kSwipeSlopPx   = 10;
/// Доля ширины, после которой отпускание досылает лист до конца.
constexpr float kSwipeCommit   = 0.25f;
/// Постоянная времени доезда, с. Экспонента по времени, а не по кадрам:
/// частота кадров тут гуляет от 10 до 20, и привязка к кадру делала бы
/// скорость анимации разной на разных экранах.
constexpr float kSwipeTau      = 0.075f;
/// Скорость, при которой жест считается броском и досылается до конца даже
/// если палец прошёл меньше порога по расстоянию, px/с.
constexpr float kSwipeFlick    = 350.0f;
/// Сколько отсутствие касания должно продержаться, чтобы считаться
/// отпусканием, мс.
///
/// Без этой задержки любая потеря точки контроллером выглядела как
/// отпускание: запускался доезд, а на следующем кадре палец обнаруживался
/// снова и смещение скачком возвращалось к сырому. Именно так возникали
/// рывки и перескоки в начальную позицию, особенно у края панели — там
/// CST9217 теряет точку чаще всего.
constexpr uint32_t kSwipeReleaseMs = 80;

struct Swipe {
    enum class Phase : uint8_t {
        Idle,      ///< касания нет
        Tracking,  ///< палец на экране, порог смещения ещё не пройден
        Dragging,  ///< экран едет за пальцем
        Coasting,  ///< палец отпущен, лист доезжает сам
    };

    Phase    phase   = Phase::Idle;
    /// Экранная X, которой соответствует нулевое смещение. Не «точка начала
    /// касания»: при подхвате пальца во время доезда сдвигается так, чтобы
    /// смещение осталось непрерывным.
    int16_t  anchorX = 0;
    float    offset  = 0.0f;   ///< текущий сдвиг в px: <0 листаем вперёд
    float    target  = 0.0f;   ///< куда доезжаем после отпускания
    int8_t   nextIdx = -1;     ///< экран, выезжающий навстречу
    float    velocity = 0.0f;  ///< сглажённая скорость пальца, px/с
    uint32_t lostMs  = 0;      ///< сколько уже нет касания
    uint32_t lastMs  = 0;      ///< время предыдущего кадра
} s_swipe;

void showFatal(const char* msg) {
    Serial.printf("[fatal] %s\n", msg);
    if (Arduino_GFX* g = Display::gfx()) {
        g->fillScreen(0x0000);
        g->setTextWrap(true);
        Text::useBuiltin(g);
        g->setTextSize(2);
        g->setTextColor(0xF800);
        g->setCursor(20, 200);
        g->print(msg);
        Display::flush();
    }
}

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Индекс соседнего экрана с зацикливанием.
uint8_t neighbour(uint8_t from, int dir, uint8_t count) {
    return static_cast<uint8_t>((from + count + dir) % count);
}

/// Куда доезжать после отпускания: до соседнего экрана или назад.
///
/// Решение по расстоянию ИЛИ по скорости. Только по расстоянию было мало:
/// короткий быстрый бросок — привычный жест, и откатывать его назад
/// воспринимается как «свайп не сработал».
void decideSwipeTarget() {
    const bool byDistance = fabsf(s_swipe.offset) > LCD_WIDTH * kSwipeCommit;

    // Бросок учитываем только если палец двигался В ту же сторону, куда уже
    // уехал экран. Иначе рывок назад в конце жеста досылал бы лист вперёд.
    const bool sameWay = (s_swipe.velocity < 0.0f) == (s_swipe.offset < 0.0f);
    const bool byFlick = sameWay && fabsf(s_swipe.velocity) > kSwipeFlick;

    s_swipe.target = (byDistance || byFlick)
        ? (s_swipe.offset < 0.0f ? -static_cast<float>(LCD_WIDTH)
                                 :  static_cast<float>(LCD_WIDTH))
        : 0.0f;
}

/// Обработка касания. Возвращает true, если идёт листание.
bool updateSwipe(uint8_t screens, uint32_t nowMs) {
    using Phase = Swipe::Phase;

    if (screens < 2) return false;

    const Touch::Point tp = Touch::poll();

    uint32_t stepMs = nowMs - s_swipe.lastMs;
    s_swipe.lastMs = nowMs;
    // Первый кадр жеста и длинные заминки не должны давать выброс скорости
    if (stepMs == 0 || stepMs > 250) stepMs = 16;
    const float dt = stepMs / 1000.0f;

    if (tp.pressed) {
        s_swipe.lostMs = 0;

        if (s_swipe.phase == Phase::Idle) {
            s_swipe.phase    = Phase::Tracking;
            s_swipe.anchorX  = tp.x;
            s_swipe.offset   = 0.0f;
            s_swipe.velocity = 0.0f;
        } else if (s_swipe.phase == Phase::Coasting) {
            // Палец вернулся посреди доезда: подхватываем лист с того места,
            // где он сейчас, а не с начала жеста.
            s_swipe.phase   = Phase::Dragging;
            s_swipe.anchorX = static_cast<int16_t>(tp.x - lroundf(s_swipe.offset));
        }

        const float raw = static_cast<float>(tp.x - s_swipe.anchorX);

        if (s_swipe.phase == Phase::Tracking) {
            if (fabsf(raw) < kSwipeSlopPx) return false;
            s_swipe.phase = Phase::Dragging;
            // Порог вычитаем из точки отсчёта, иначе лист прыгнет сразу на
            // его величину вместо того чтобы поехать от нуля.
            s_swipe.anchorX += static_cast<int16_t>(raw > 0.0f ? kSwipeSlopPx
                                                              : -kSwipeSlopPx);
        }

        const float prev = s_swipe.offset;
        s_swipe.offset = clampf(static_cast<float>(tp.x - s_swipe.anchorX),
                                -static_cast<float>(LCD_WIDTH),
                                 static_cast<float>(LCD_WIDTH));

        // Скорость сглаживаем: одиночный выброс координаты не должен решать
        // судьбу жеста, а контроллер их даёт.
        const float inst = (s_swipe.offset - prev) / dt;
        s_swipe.velocity += (inst - s_swipe.velocity) * 0.35f;

        // Направление берём от текущего смещения, а не от начального рывка:
        // если передумать и повести палец обратно, навстречу должен выезжать
        // другой экран. Около нуля не трогаем, чтобы не мигало.
        if (fabsf(s_swipe.offset) >= kSwipeSlopPx) {
            s_swipe.nextIdx = neighbour(s_screen, s_swipe.offset < 0.0f ? 1 : -1,
                                        screens);
        }
        return true;
    }

    // Касания нет
    if (s_swipe.phase == Phase::Tracking) {
        s_swipe.phase = Phase::Idle;
        return false;
    }

    if (s_swipe.phase == Phase::Dragging) {
        // Потеря точки на пару кадров — это не отпускание. Держим лист на
        // месте и ждём: либо палец вернётся, либо истечёт задержка.
        s_swipe.lostMs += stepMs;
        if (s_swipe.lostMs < kSwipeReleaseMs) return true;

        decideSwipeTarget();
        s_swipe.phase = Phase::Coasting;
    }

    if (s_swipe.phase != Phase::Coasting) return false;

    // Доезд по экспоненте от времени: одинаковый на 10 и на 20 кадрах в секунду
    s_swipe.offset += (s_swipe.target - s_swipe.offset) *
                      (1.0f - expf(-dt / kSwipeTau));

    if (fabsf(s_swipe.target - s_swipe.offset) < 1.0f) {
        if (s_swipe.target != 0.0f && s_swipe.nextIdx >= 0) {
            s_screen = static_cast<uint8_t>(s_swipe.nextIdx);
            Serial.printf("[main] экран %u: %s\n", s_screen, Layout::screenName(s_screen));
        }
        const uint32_t keepMs = s_swipe.lastMs;
        s_swipe = Swipe{};
        s_swipe.lastMs = keepMs;
        return false;
    }
    return true;
}

// ─── Композиция кадра из двух слоёв ─────────────────────────────────────────

/// Экран, чьё неподвижное содержимое лежит в статическом слое.
constexpr uint8_t kNoStatic = 0xFF;
uint8_t s_staticScreen = kNoStatic;

/// Сдвиг сцены, при котором слой был построен. Защита от выгорания раз в
/// несколько минут смещает всю сцену, и слой после этого недействителен.
int16_t s_staticShiftX = 0;
int16_t s_staticShiftY = 0;

/// В предыдущем кадре был полноэкранный оверлей (авария, вспышка шифт-лайта).
/// Он затронул пиксели за пределами рамок виджетов, поэтому восстанавливать
/// надо весь экран, а не прямоугольники.
bool s_overlayLast = false;

/// Этот кадр надо отправить на панель целиком, а не полосами.
bool s_fullFlush = true;

/**
 * Кадр без полной перерисовки.
 *
 * Неподвижная часть экрана рисуется один раз и запоминается. Дальше каждый
 * кадр из слоя копируется фон только под движущимися виджетами — вместо
 * очистки всех 466x466 пикселей и повторной отрисовки подписей.
 *
 * Если статический слой выделить не удалось, поведение откатывается к полной
 * перерисовке: результат тот же, просто дороже.
 */
bool drawComposited(const Render::Frame& frame, const BurnIn::Shift& shift) {
    if (!Display::staticLayerReady()) {
        Display::clear(frame.bg);
        s_overlayLast = Render::screen(frame, s_screen);
        return true;
    }

    const bool layerStale = s_staticScreen != s_screen ||
                            s_staticShiftX != shift.x ||
                            s_staticShiftY != shift.y;

    // Полная отправка нужна там, где изменилось не только под виджетами:
    // при перестройке слоя обновляется весь экран, а после полноэкранного
    // оверлея восстанавливается тоже весь.
    bool full = layerStale || s_overlayLast;

    if (layerStale) {
        Display::clear(frame.bg);
        Render::screen(frame, s_screen, Render::Pass::Static);
        Display::saveStaticLayer();

        s_staticScreen = s_screen;
        s_staticShiftX = shift.x;
        s_staticShiftY = shift.y;
    } else if (s_overlayLast) {
        Display::restoreAll();
    } else {
        Render::restoreDynamic(frame, s_screen);
    }

    s_overlayLast = Render::screen(frame, s_screen, Render::Pass::Dynamic);
    return full || s_overlayLast;
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\nTracraft Dash Round — boot");

    if (!Display::begin()) {
        Serial.println("[fatal] дисплей не инициализирован");
        return;
    }

    Display::selfTest();

    BurnIn::Config burnInCfg;
    burnInCfg.enabled          = true;
    burnInCfg.shiftIntervalSec = 180;   // смена позиции орбиты каждые 3 минуты
    burnInCfg.shiftMagnitude   = 2;     // +-2 px, на 466 px незаметно
    burnInCfg.refreshEveryH    = 24;
    BurnIn::init(burnInCfg);

    // Тач не критичен: без него остаётся автолистание по таймеру,
    // поэтому неудача здесь не останавливает загрузку.
    Touch::begin();

    s_haveLayout = Layout::load();
    if (!s_haveLayout) {
        showFatal("NO LAYOUT\nrun: pio run -t uploadfs");
        return;
    }

    s_simOn = Layout::simEnabled();
    Serial.printf("[main] симуляция: %s\n", s_simOn ? "включена" : "выключена");
    if (!s_simOn) {
        Serial.println("[main] CAN и датчики ещё не подключены — сигналы будут нулевыми.");
        Serial.println("[main] включи чекбокс Sim в редакторе и перезалей лейаут.");
    }

    // Логи больше не имеют права останавливать кадр.
    //
    // Это оказалось главной причиной падения до 0.5 FPS. Serial здесь — это
    // HWCDC поверх USB Serial/JTAG. Когда монитор закрыт, но кабель остался в
    // порту, USB продолжает жить: isPlugged() возвращает true, CDC считает
    // себя подключённым, а читать данные никто не читает. Кольцевой буфер
    // заполняется, и HWCDC::write уходит в ожидание — 100 мс на попытку, до 20
    // попыток, то есть до двух секунд на один printf. Ровно те 0.5 FPS,
    // которые наблюдались без монитора и исчезали при его открытии.
    //
    // Нулевой таймаут переводит запись в режим «не влезло — выбросили».
    // Ставится после setup() намеренно: загрузочные сообщения и результат
    // selfTest() должны дойти, если монитор уже открыт, а вот в цикле кадра
    // ни одна строка лога не стоит пропущенного кадра.
    Serial.setTxTimeoutMs(0);

    s_bootMs       = millis();
    s_lastFpsMs    = s_bootMs;
    s_lastSwitchMs = s_bootMs;
}

void loop() {
    if (!s_haveLayout) {
        delay(1000);
        return;
    }

    const uint32_t now = millis();
    const float    t   = (now - s_bootMs) / 1000.0f;

    // 1. Данные
    if (s_simOn) Sim::update(t);

    // 2. Кадр
    BurnIn::update(now);
    const BurnIn::Shift shift = BurnIn::getShift();

    const uint8_t screens = Layout::screenCount();
    const bool    sliding = updateSwipe(screens, now);

    const uint32_t tDraw0 = micros();

    const uint16_t bg = Layout::screenBg(s_screen);

    // Сдвиг листания складывается со сдвигом от защиты выгорания: и то и
    // другое — просто смещение всей сцены, одним и тем же полем Frame.
    const int16_t slide = static_cast<int16_t>(lroundf(s_swipe.offset));

    const Render::Frame frame{Display::gfx(), t,
                              static_cast<int16_t>(shift.x + slide),
                              shift.y, bg};

    if (sliding) {
        // Во время листания в кадре два экрана со своими смещениями, и от
        // кадра к кадру смещения меняются — статический слой тут бесполезен,
        // рисуем всё честно и помечаем слой недействительным.
        Display::clear(bg);
        Render::screen(frame, s_screen);

        // Второй экран выезжает навстречу: он стоит вплотную за первым,
        // поэтому его смещение отличается ровно на ширину панели.
        if (s_swipe.nextIdx >= 0) {
            const int16_t side = slide < 0 ? LCD_WIDTH : -LCD_WIDTH;
            const uint8_t next = static_cast<uint8_t>(s_swipe.nextIdx);
            Render::screen(Render::Frame{Display::gfx(), t,
                                         static_cast<int16_t>(shift.x + slide + side),
                                         shift.y, Layout::screenBg(next)},
                           next);
        }
        s_staticScreen = kNoStatic;
        s_fullFlush    = true;
    } else {
        s_fullFlush = drawComposited(frame, shift);
    }

    const uint32_t drawUs = micros() - tDraw0;

    // 3. На панель. Отправка — самая дорогая часть кадра, поэтому неподвижные
    //    строки на панель не гоним: полосы под движущимися виджетами обычно
    //    занимают половину экрана или меньше.
    const uint32_t tFlush0 = micros();
    if (s_fullFlush) {
        Display::flush();
    } else {
        Render::Band bands[Render::kMaxBands];
        const int nb = Render::dirtyBands(frame, s_screen, bands, Render::kMaxBands);
        if (nb == 0) {
            Display::flush();
        } else {
            for (int i = 0; i < nb; ++i) {
                Display::flushRows(bands[i].y0, bands[i].y1 - bands[i].y0);
            }
        }
    }
    const uint32_t flushUs = micros() - tFlush0;

    s_drawSumUs  += drawUs;
    s_flushSumUs += flushUs;
    if (drawUs  > s_drawMaxUs)  s_drawMaxUs  = drawUs;
    if (flushUs > s_flushMaxUs) s_flushMaxUs = flushUs;

    // Если тач не поднялся — оставляем прежнюю карусель по таймеру, иначе
    // экраны листаются только пальцем.
    if (!Touch::available() && screens > 1 && now - s_lastSwitchMs >= kScreenDwellMs) {
        s_lastSwitchMs = now;
        s_screen = (s_screen + 1) % screens;
        Serial.printf("[main] экран %u: %s\n", s_screen, Layout::screenName(s_screen));
    }

    ++s_frames;
    if (now - s_lastFpsMs >= 1000) {
        const uint32_t f = s_frames ? s_frames : 1;
        Serial.printf("[perf] %lu FPS | рендер %lu мс (макс %lu) | отправка %lu мс (макс %lu)\n",
                      static_cast<unsigned long>(s_frames),
                      static_cast<unsigned long>(s_drawSumUs / f / 1000),
                      static_cast<unsigned long>(s_drawMaxUs / 1000),
                      static_cast<unsigned long>(s_flushSumUs / f / 1000),
                      static_cast<unsigned long>(s_flushMaxUs / 1000));

        s_frames     = 0;
        s_drawSumUs  = 0;
        s_drawMaxUs  = 0;
        s_flushSumUs = 0;
        s_flushMaxUs = 0;
        s_lastFpsMs  = now;
    }
}
