/**
 * @file  gfx_util.h
 * @brief Мелкие помощники отрисовки, общие для загрузчика и рендера.
 */

#pragma once

#include <Arduino.h>

/// "#RRGGBB" → RGB565. Некорректная строка → fallback.
inline uint16_t rgb565FromHex(const char* hex, uint16_t fallback) {
    if (!hex) return fallback;
    if (*hex == '#') ++hex;
    if (strlen(hex) < 6) return fallback;

    char* end = nullptr;
    const long v = strtol(hex, &end, 16);
    if (end != hex + 6) return fallback;

    const uint8_t r = (v >> 16) & 0xFF;
    const uint8_t g = (v >> 8) & 0xFF;
    const uint8_t b = v & 0xFF;
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/// Линейная интерполяция двух RGB565 — для затемнения и полупрозрачности.
inline uint16_t blend565(uint16_t a, uint16_t b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;

    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;

    const int r = ar + static_cast<int>((br - ar) * t);
    const int g = ag + static_cast<int>((bg - ag) * t);
    const int bl = ab + static_cast<int>((bb - ab) * t);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}
