#include "display_hw.h"
#include "gfx_util.h"
#include "pins.h"
#include "qspi_dma.h"

#include <esp_heap_caps.h>
#include <string.h>

namespace Display {

namespace {

/**
 * Канва с фреймбуфером в PSRAM.
 *
 * Зачем подкласс: Arduino_Canvas::begin() выделяет буфер через
 * aligned_alloc(), то есть из ВНУТРЕННЕЙ памяти. 466*466*2 = 434 КБ одним
 * куском во внутренней RAM ESP32-S3 не выделяются никогда — begin() вернул бы
 * false. Поле _framebuffer объявлено protected, поэтому подкласс может
 * положить туда буфер из PSRAM заранее: begin() увидит непустой указатель и
 * свой alloc пропустит.
 */
class PsramCanvas : public Arduino_Canvas {
public:
    using Arduino_Canvas::Arduino_Canvas;

    uint16_t* raw() { return _framebuffer; }

    bool reserve(size_t bytes) {
        if (_framebuffer) return true;
        // Выравнивание 16 байт — требование DMA при передаче из PSRAM.
        _framebuffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM));
        return _framebuffer != nullptr;
    }

    // ─── Pre-swapped storage ──────────────────────────────────────────────
    //
    // Проблема: writePixels читал фреймбуфер из PSRAM, переставлял байты в
    // каждом пикселе и писал в DMA-буфер в SRAM — CPU-нагрузка ~23 мс на кадр,
    // которая ни с чем не перекрывалась.
    //
    // Решение: хранить пиксели в PSRAM уже в порядке байт панели (big-endian).
    // Тогда writePixels пускает DMA прямо с указателя на PSRAM — нулевая
    // CPU-нагрузка в пути данных.
    //
    // Цена: каждый примитив GFX хранит swap16(color) вместо color. Одна
    // инструкция на пиксель против чтения всего кадра из PSRAM — выгодно.
    // aa_font при чтении фона для смешивания разворачивает байты обратно.
    //
    // Перехватывать надо ВСЕ четыре виртуальных метода записи Arduino_Canvas.
    // Одних writePixelPreclipped и writeFillRectPreclipped не хватает: быстрые
    // линии — отдельная ветка, и через неё идёт очень много отрисовки
    // (fillCircle внутри GFX построен на writeFastVLine, туда же рамки,
    // сетки графика, треугольники). Пропуск этой ветки и разваливал цвета.
    //
    // Каждый override делегирует в базовую реализацию с уже переставленным
    // цветом: так наследуется вся логика отсечения и поворота, а нам остаётся
    // только один swap16 на вызов, а не на пиксель.

    void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
        Arduino_Canvas::writePixelPreclipped(x, y, swap16(color));
    }

    void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        Arduino_Canvas::writeFastVLine(x, y, h, swap16(color));
    }

    void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        Arduino_Canvas::writeFastHLine(x, y, w, swap16(color));
    }

    void writeFillRectPreclipped(int16_t x, int16_t y,
                                 int16_t w, int16_t h, uint16_t color) override {
        Arduino_Canvas::writeFillRectPreclipped(x, y, w, h, swap16(color));
    }
};

/// Шина своя, а не библиотечная Arduino_ESP32QSPI — см. qspi_dma.h: там
/// отправка пикселей идёт конвейером, а не по очереди с их подготовкой.
/// Тип конкретный, а не Arduino_DataBus*, чтобы читать счётчик ожидания.
Arduino_ESP32QSPI_DMA* s_bus    = nullptr;
Arduino_GFX*           s_panel  = nullptr;
PsramCanvas*           s_canvas = nullptr;

/// Копия кадра с одной только неподвижной частью экрана: фон и виджеты,
/// которые не зависят от сигналов. Позволяет не перерисовывать их каждый кадр,
/// а восстанавливать фон под движущимися виджетами копированием.
uint16_t* s_static = nullptr;

constexpr size_t  kFbBytes = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * 2;
constexpr int32_t kQspiHz  = 80000000;

} // namespace

bool begin() {
    // Питание панели — до begin(), иначе контроллер не ответит.
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);

    s_bus = new Arduino_ESP32QSPI_DMA(
        LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

    // В GFX 1.6.7 у обоих драйверов нет параметра IPS — примеры LilyGo
    // написаны под 1.3.7, где он ещё был. Порядок: bus, rst, rotation, w, h,
    // затем четыре смещения.
#if defined(PANEL_SH8601)
    // Резервный вариант, если карточка товара всё же права.
    // Смещения нулевые — так же, как в примере LilyGo для SH8601.
    s_panel = new Arduino_SH8601(
        s_bus, LCD_RST, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT);
#else
    s_panel = new Arduino_CO5300(
        s_bus, LCD_RST, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT,
        LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0);
#endif

    s_canvas = new PsramCanvas(LCD_WIDTH, LCD_HEIGHT, s_panel);

    if (!s_canvas->reserve(kFbBytes)) {
        Serial.printf("[display] PSRAM: не выделено %u КБ под фреймбуфер\n",
                      static_cast<unsigned>(kFbBytes / 1024));
        Serial.printf("[display] свободно в PSRAM: %u КБ\n",
                      static_cast<unsigned>(
                          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        return false;
    }

    // 80 МГц вместо дефолтных 40: на четырёх линиях это ~40 МБ/с, то есть
    // кадр 424 КБ уходит примерно за 11 мс вместо 21. Контроллеры CO5300 и
    // SH8601 такую частоту держат. Если на панели появятся артефакты или
    // «снег» — снизить до 40000000, это первый подозреваемый.
    if (!s_canvas->begin(kQspiHz)) {
        Serial.printf("[display] canvas begin() не удался на %d МГц\n", kQspiHz / 1000000);
        return false;
    }

    // Статический слой не критичен: без него всё работает, просто каждый кадр
    // перерисовывается целиком. Поэтому неудача выделения не роняет запуск.
    s_static = static_cast<uint16_t*>(
        heap_caps_aligned_alloc(16, kFbBytes, MALLOC_CAP_SPIRAM));
    if (!s_static) {
        Serial.println("[display] статический слой не выделен, "
                       "перерисовка будет полной");
    }

    s_canvas->fillScreen(RGB565_BLACK);
    s_canvas->flush();
    setBrightness(255);

    Serial.printf("[display] CO5300 %dx%d, фреймбуфер %u КБ в PSRAM, QSPI %d МГц\n",
                  LCD_WIDTH, LCD_HEIGHT,
                  static_cast<unsigned>(kFbBytes / 1024),
                  kQspiHz / 1000000);
    return true;
}

void selfTest() {
    uint16_t* fb = s_canvas ? s_canvas->raw() : nullptr;
    if (!fb) return;

    // Замеряем три вещи по отдельности, потому что рассуждать о том, где
    // теряются кадры, без чисел бессмысленно:
    //   1. запись в PSRAM     — потолок любой отрисовки
    //   2. отправка кадра     — потолок вывода, ниже него FPS не поднять
    //   3. примитив с клипом  — цена одного вызова рисования
    constexpr int kRuns = 5;

    uint32_t t0 = micros();
    for (int i = 0; i < kRuns; ++i) clear(i & 1 ? 0xFFFF : 0x0000);
    const uint32_t clearUs = (micros() - t0) / kRuns;

    t0 = micros();
    for (int i = 0; i < kRuns; ++i) s_canvas->flush();
    const uint32_t flushUs = (micros() - t0) / kRuns;

    t0 = micros();
    for (int i = 0; i < kRuns; ++i) s_canvas->fillCircle(233, 233, 200, 0x07E0);
    const uint32_t circleUs = (micros() - t0) / kRuns;

    const float mbPerSec = kFbBytes / 1048576.0f / (clearUs / 1000000.0f);

    Serial.println("[selftest] --- потолки производительности ---");
    Serial.printf("[selftest] очистка PSRAM : %6lu мкс  (%.1f МБ/с)\n",
                  static_cast<unsigned long>(clearUs), mbPerSec);
    Serial.printf("[selftest] отправка кадра: %6lu мкс  (потолок %.0f FPS)\n",
                  static_cast<unsigned long>(flushUs),
                  flushUs ? 1000000.0f / flushUs : 0.0f);
    Serial.printf("[selftest] круг r=200    : %6lu мкс\n",
                  static_cast<unsigned long>(circleUs));
    Serial.printf("[selftest] PSRAM свободно: %u КБ, heap %u КБ\n",
                  static_cast<unsigned>(ESP.getFreePsram() / 1024),
                  static_cast<unsigned>(ESP.getFreeHeap() / 1024));

    clear(RGB565_BLACK);
    s_canvas->flush();
}

void clear(uint16_t color) {
    uint16_t* fb = s_canvas ? s_canvas->raw() : nullptr;
    if (!fb) return;

    // Пишем по 32 бита вместо 16: вдвое меньше обращений к PSRAM, а она здесь
    // узкое место. Fb выровнен на 16 байт, так что доступ по uint32_t корректен.
    // Цвет хранится pre-swapped (big-endian) — так же, как остальные пиксели.
    const uint16_t s = swap16(color);
    const uint32_t pair = (static_cast<uint32_t>(s) << 16) | s;
    auto* p32 = reinterpret_cast<uint32_t*>(fb);
    const size_t n32 = (static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT) / 2;
    for (size_t i = 0; i < n32; ++i) p32[i] = pair;
}

Arduino_GFX* gfx() { return s_canvas; }

void flush() {
    if (s_canvas) s_canvas->flush();
}

void flushRows(int y0, int h) {
    uint16_t* fb = framebuffer();
    if (!fb || !s_panel || h <= 0) return;

    // Некоторые контроллеры этого класса требуют чётных границ окна
    // адресации, поэтому расширяем полосу до чётных строк, а не сужаем.
    if (y0 & 1) { --y0; ++h; }
    if (h & 1) ++h;

    if (y0 < 0) { h += y0; y0 = 0; }
    if (y0 + h > LCD_HEIGHT) h = LCD_HEIGHT - y0;
    if (h <= 0) return;

    // Полоса во всю ширину лежит в памяти непрерывно, поэтому библиотека
    // отправит её одним writePixels — см. Arduino_TFT::draw16bitRGBBitmap.
    s_panel->draw16bitRGBBitmap(0, y0, fb + static_cast<size_t>(y0) * LCD_WIDTH,
                                LCD_WIDTH, h);
}

uint32_t busWaitedUs() {
    if (!s_bus) return 0;
    const uint32_t v = s_bus->waitedUs();
    s_bus->resetWaited();
    return v;
}

void setBrightness(uint8_t v) {
#if defined(PANEL_SH8601)
    if (auto* p = static_cast<Arduino_SH8601*>(s_panel)) p->setBrightness(v);
#else
    if (auto* p = static_cast<Arduino_CO5300*>(s_panel)) p->setBrightness(v);
#endif
}

size_t framebufferBytes() { return kFbBytes; }

uint16_t* framebuffer() { return s_canvas ? s_canvas->raw() : nullptr; }

// ─── Статический слой ────────────────────────────────────────────────────────

bool staticLayerReady() { return s_static != nullptr; }

void saveStaticLayer() {
    uint16_t* fb = framebuffer();
    if (!fb || !s_static) return;
    memcpy(s_static, fb, kFbBytes);
}

void restoreRect(int x, int y, int w, int h) {
    uint16_t* fb = framebuffer();
    if (!fb || !s_static) return;

    // Клип по границам: прямоугольники приходят уже со сдвигом защиты от
    // выгорания, поэтому вылезти за край — норма, а не ошибка вызывающего.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    const size_t rowBytes = static_cast<size_t>(w) * 2;
    for (int row = 0; row < h; ++row) {
        const size_t off = static_cast<size_t>(y + row) * LCD_WIDTH + x;
        memcpy(fb + off, s_static + off, rowBytes);
    }
}

void restoreAll() {
    uint16_t* fb = framebuffer();
    if (!fb || !s_static) return;
    memcpy(fb, s_static, kFbBytes);
}

} // namespace Display
