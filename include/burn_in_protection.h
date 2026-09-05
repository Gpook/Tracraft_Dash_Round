/**
 * @file    burn_in_protection.h
 * @brief   AMOLED burn-in protection — CO5300 466×466.
 *
 * Только геометрические методы — яркость не трогаем, она всегда максимум.
 *
 * Метод 1 — Pixel Shift (Orbit)
 *   Каждые shiftIntervalSec сек весь рендер смещается на ±N px по
 *   9-позиционной орбите. Сдвиг 2 px на 466 px дисплее визуально незаметен,
 *   но нагрузка на каждый пиксель распределяется равномернее.
 *
 * Метод 2 — Pixel Refresh
 *   Раз в refreshEveryH часов (обычно при выключённом зажигании) запускается
 *   sweep: белый → чёрный → серый → чёрный. Выравнивает усталость OLED-слоя.
 *   Функция блокирующая — запускай в отдельной задаче FreeRTOS.
 *
 * Использование в рендер-цикле:
 *   BurnIn::update(millis());                    // вызвать каждый кадр
 *   auto s = BurnIn::getShift();                 // dx, dy → добавить к translate
 *   bool needRefresh = BurnIn::needsRefresh();   // true → запустить runPixelRefresh()
 */

#pragma once
#include <Arduino.h>

namespace BurnIn {

// ─── Конфигурация ─────────────────────────────────────────────────────────────

struct Config {
    bool     enabled          = true;

    // Pixel Shift
    uint16_t shiftIntervalSec = 180;  ///< Интервал смены позиции, сек (default 3 мин)
    uint8_t  shiftMagnitude   = 2;    ///< Радиус орбиты, px (0 = выкл, рекомендуется 2)

    // Pixel Refresh
    uint8_t  refreshEveryH    = 24;   ///< Интервал refresh, часы (0 = выкл)
};

// ─── Сдвиг рендера ────────────────────────────────────────────────────────────

struct Shift { int8_t x; int8_t y; };

/**
 * 9 позиций орбиты. Нет повторов, нет осевой симметрии —
 * равномерный износ по всем направлениям.
 */
static constexpr Shift ORBIT[9] = {
    { 0,  0},
    { 2,  0},
    { 1,  2},
    {-1,  2},
    {-2,  0},
    {-2, -1},
    { 0, -2},
    { 2, -1},
    { 1,  1},
};
static constexpr uint8_t ORBIT_LEN = 9;

// ─── API ──────────────────────────────────────────────────────────────────────

void  init(const Config& cfg);

/**
 * Вызывать каждый кадр (или реже — 1 Гц достаточно).
 * @param nowMs  millis()
 * @return true если позиция орбиты изменилась → рендер нужно переотрисовать
 */
bool  update(uint32_t nowMs);

/** Текущий сдвиг. Применить к ctx.translate(dx, dy) перед отрисовкой виджетов. */
Shift getShift();

/** true → надо запустить runPixelRefresh() (обычно при выключении зажигания). */
bool  needsRefresh();

/**
 * Pixel Refresh sweep: белый → чёрный → серый → чёрный.
 * БЛОКИРУЮЩАЯ ~500 мс. Запускать в задаче FreeRTOS, не в loop/ISR.
 *
 * @param setWindow  fn(xs, ys, xe, ye) — установить окно вывода на дисплей
 * @param pushColor  fn(color_rgb565, count) — залить N пикселей одним цветом
 */
using SetWindowFn = void(*)(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
using PushColorFn = void(*)(uint16_t color, uint32_t count);

void runPixelRefresh(SetWindowFn setWindow, PushColorFn pushColor,
                     uint16_t dispW, uint16_t dispH);

// ─── Internal ─────────────────────────────────────────────────────────────────
namespace _impl {
    extern Config   g_cfg;
    extern uint32_t g_lastShiftMs;
    extern uint32_t g_lastRefreshMs;
    extern uint8_t  g_orbitPhase;
    extern bool     g_refreshPending;
}

} // namespace BurnIn
