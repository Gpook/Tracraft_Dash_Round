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
constexpr int   kSwipeSlopPx   = 12;
/// Доля ширины, после которой отпускание досылает лист до конца.
constexpr float kSwipeCommit   = 0.28f;
/// Доля пути, догоняемая за кадр при инерции. 0.22 на 30 FPS даёт ~120 мс.
constexpr float kSwipeEase     = 0.22f;

struct Swipe {
    bool    active   = false;   ///< палец на экране и жест распознан
    bool    tracking = false;   ///< палец на экране, но порог ещё не пройден
    int16_t startX   = 0;
    float   offset   = 0.0f;    ///< текущий сдвиг в px: <0 листаем вперёд
    float   target   = 0.0f;    ///< куда доезжаем после отпускания
    int8_t  nextIdx  = -1;      ///< экран, выезжающий навстречу
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

/// Индекс соседнего экрана с зацикливанием.
uint8_t neighbour(uint8_t from, int dir, uint8_t count) {
    return static_cast<uint8_t>((from + count + dir) % count);
}

/// Обработка касания. Возвращает true, если идёт анимация листания.
bool updateSwipe(uint8_t screens) {
    if (screens < 2) return false;

    const Touch::Point tp = Touch::poll();

    if (tp.pressed) {
        if (!s_swipe.tracking && !s_swipe.active) {
            s_swipe.tracking = true;
            s_swipe.startX   = tp.x;
        }

        const int dx = tp.x - s_swipe.startX;

        if (s_swipe.tracking && abs(dx) >= kSwipeSlopPx) {
            s_swipe.tracking = false;
            s_swipe.active   = true;
        }

        if (s_swipe.active) {
            s_swipe.offset  = static_cast<float>(dx);
            // Тянем влево — следующий экран приходит справа, и наоборот
            s_swipe.nextIdx = neighbour(s_screen, dx < 0 ? 1 : -1, screens);
        }
        return s_swipe.active;
    }

    // Палец отпущен
    if (s_swipe.tracking) {
        s_swipe.tracking = false;
        return false;
    }

    if (!s_swipe.active) return false;

    // Решение принимается один раз, в момент отпускания
    if (s_swipe.target == 0.0f && fabsf(s_swipe.offset) > 0.5f) {
        const bool commit = fabsf(s_swipe.offset) > LCD_WIDTH * kSwipeCommit;
        s_swipe.target = commit
            ? (s_swipe.offset < 0.0f ? -static_cast<float>(LCD_WIDTH)
                                     :  static_cast<float>(LCD_WIDTH))
            : 0.0f;
    }

    // Доезд по экспоненте: быстро в начале, мягко в конце
    s_swipe.offset += (s_swipe.target - s_swipe.offset) * kSwipeEase;

    if (fabsf(s_swipe.target - s_swipe.offset) < 1.5f) {
        if (s_swipe.target != 0.0f && s_swipe.nextIdx >= 0) {
            s_screen = static_cast<uint8_t>(s_swipe.nextIdx);
            Serial.printf("[main] экран %u: %s\n", s_screen, Layout::screenName(s_screen));
        }
        s_swipe = Swipe{};   // сброс всего состояния жеста
        return false;
    }
    return true;
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
    const bool    sliding = updateSwipe(screens);

    const uint32_t tDraw0 = micros();

    const uint16_t bg = Layout::screenBg(s_screen);
    Display::clear(bg);

    // Сдвиг листания складывается со сдвигом от защиты выгорания: и то и
    // другое — просто смещение всей сцены, одним и тем же полем Frame.
    const int16_t slide = static_cast<int16_t>(lroundf(s_swipe.offset));

    Render::screen(Render::Frame{Display::gfx(), t,
                                 static_cast<int16_t>(shift.x + slide),
                                 shift.y, bg},
                   s_screen);

    // Второй экран выезжает навстречу: он стоит вплотную за первым, поэтому
    // его смещение отличается ровно на ширину панели.
    if (sliding && s_swipe.nextIdx >= 0) {
        const int16_t side = slide < 0 ? LCD_WIDTH : -LCD_WIDTH;
        const uint8_t next = static_cast<uint8_t>(s_swipe.nextIdx);
        Render::screen(Render::Frame{Display::gfx(), t,
                                     static_cast<int16_t>(shift.x + slide + side),
                                     shift.y, Layout::screenBg(next)},
                       next);
    }

    const uint32_t drawUs = micros() - tDraw0;

    // 3. На панель
    const uint32_t tFlush0 = micros();
    Display::flush();
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
