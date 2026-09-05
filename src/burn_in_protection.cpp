/**
 * @file  burn_in_protection.cpp
 * @brief AMOLED burn-in protection — pixel shift + pixel refresh.
 *        Яркость не изменяется.
 */

#include "burn_in_protection.h"

namespace BurnIn::_impl {
    Config   g_cfg;
    uint32_t g_lastShiftMs    = 0;
    uint32_t g_lastRefreshMs  = 0;
    uint8_t  g_orbitPhase     = 0;
    bool     g_refreshPending = false;
}

using namespace BurnIn::_impl;

// ─────────────────────────────────────────────────────────────────────────────

void BurnIn::init(const Config& cfg) {
    g_cfg             = cfg;
    g_lastShiftMs     = millis();
    g_lastRefreshMs   = millis();
    g_orbitPhase      = 0;
    g_refreshPending  = false;
}

bool BurnIn::update(uint32_t nowMs) {
    if (!g_cfg.enabled) return false;

    bool changed = false;

    // ── Pixel shift ──────────────────────────────────────────────────────────
    if (g_cfg.shiftMagnitude > 0 && g_cfg.shiftIntervalSec > 0) {
        const uint32_t intervalMs = (uint32_t)g_cfg.shiftIntervalSec * 1000UL;
        if (nowMs - g_lastShiftMs >= intervalMs) {
            g_orbitPhase  = (g_orbitPhase + 1) % ORBIT_LEN;
            g_lastShiftMs = nowMs;
            changed       = true;
        }
    }

    // ── Pixel refresh — только выставляем флаг, не выполняем ─────────────────
    if (!g_refreshPending && g_cfg.refreshEveryH > 0) {
        const uint32_t intervalMs = (uint32_t)g_cfg.refreshEveryH * 3600UL * 1000UL;
        if (nowMs - g_lastRefreshMs >= intervalMs) {
            g_refreshPending = true;
            // Вызывающий код сам решит, когда безопасно запустить sweep
            // (например, только при выключённом зажигании)
        }
    }

    return changed;
}

BurnIn::Shift BurnIn::getShift() {
    if (!g_cfg.enabled || g_cfg.shiftMagnitude == 0) return {0, 0};

    const auto& base = ORBIT[g_orbitPhase % ORBIT_LEN];
    const int8_t mag = static_cast<int8_t>(g_cfg.shiftMagnitude);

    // Базовая орбита нормирована под радиус 2 px
    return {
        static_cast<int8_t>(base.x * mag / 2),
        static_cast<int8_t>(base.y * mag / 2),
    };
}

bool BurnIn::needsRefresh() {
    return g_refreshPending;
}

void BurnIn::runPixelRefresh(
    SetWindowFn setWindow,
    PushColorFn pushColor,
    uint16_t dispW,
    uint16_t dispH
) {
    if (!setWindow || !pushColor) return;

    const uint32_t total = static_cast<uint32_t>(dispW) * dispH;
    setWindow(0, 0, dispW - 1, dispH - 1);

    // 1. Полная засветка — заряжаем пиксели
    pushColor(0xFFFF, total);
    delay(80);

    // 2. Полное гашение — разряжаем
    pushColor(0x0000, total);
    delay(80);

    // 3. Нейтральный серый — промежуточное состояние
    pushColor(0x8410, total);   // RGB565 ≈ (128, 128, 128)
    delay(80);

    // 4. Гасим
    pushColor(0x0000, total);

    g_lastRefreshMs  = millis();
    g_refreshPending = false;
}
