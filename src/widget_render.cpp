#include "widget_render.h"

#include "display_hw.h"
#include "gfx_util.h"
#include "layout_store.h"
#include "pins.h"
#include "signal_bus.h"
#include "text_render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Render {

namespace {

struct R { int x, y, w, h; };

// Ð’Ñ‹Ñ€Ð°Ð²Ð½Ð¸Ð²Ð°Ð½Ð¸Ðµ Ð±ÐµÑ€Ñ‘Ð¼ Ð¸Ð· Ð¼Ð¾Ð´ÑƒÐ»Ñ Ñ‚ÐµÐºÑÑ‚Ð°, Ñ‡Ñ‚Ð¾Ð±Ñ‹ Ð½Ðµ Ð´ÐµÑ€Ð¶Ð°Ñ‚ÑŒ Ð´Ð²Ð° Ð¾Ð´Ð¸Ð½Ð°ÐºÐ¾Ð²Ñ‹Ñ… enum
using VA = Text::VA;

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// â”€â”€â”€ Ð”Ð¾ÑÑ‚ÑƒÐ¿ Ðº Ð¿Ñ€Ð¾Ð¿Ð°Ð¼ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

float pF(JsonObjectConst p, const char* k, float d) { return p[k] | d; }
int   pI(JsonObjectConst p, const char* k, int d)   { return p[k] | d; }
bool  pB(JsonObjectConst p, const char* k, bool d)  { return p[k] | d; }

const char* pS(JsonObjectConst p, const char* k, const char* d) {
    const char* v = p[k] | static_cast<const char*>(nullptr);
    return (v && *v) ? v : d;
}

uint16_t pC(JsonObjectConst p, const char* k, uint16_t d) {
    return rgb565FromHex(p[k] | static_cast<const char*>(nullptr), d);
}

/// Ð¦Ð²ÐµÑ‚ Ð·Ð¾Ð½Ñ‹, Ð² ÐºÐ¾Ñ‚Ð¾Ñ€ÑƒÑŽ Ð¿Ð¾Ð¿Ð°Ð´Ð°ÐµÑ‚ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ.
uint16_t zoneColor(JsonArrayConst zones, float v, uint16_t fallback) {
    for (JsonObjectConst z : zones) {
        const float from = z["from"] | -1.0e30f;
        const float to   = z["to"]   |  1.0e30f;
        if (v >= from && v <= to) {
            return rgb565FromHex(z["color"] | static_cast<const char*>(nullptr), fallback);
        }
    }
    return fallback;
}

// â”€â”€â”€ Ð¢ÐµÐºÑÑ‚ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//
// Ð Ð°Ð·Ð¼ÐµÑ€ Ð²ÐµÐ·Ð´Ðµ Ð·Ð°Ð´Ð°Ñ‘Ñ‚ÑÑ Ð²Ñ‹ÑÐ¾Ñ‚Ð¾Ð¹ Ñ†Ð¸Ñ„Ñ€ Ð² Ð¿Ð¸ÐºÑÐµÐ»ÑÑ… â€” Ñ‚Ð¾Ð¹ Ð¶Ðµ Ð²ÐµÐ»Ð¸Ñ‡Ð¸Ð½Ð¾Ð¹, Ñ‡Ñ‚Ð¾ Ð¸
// fontSize Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ. ÐœÐ¾Ð´ÑƒÐ»ÑŒ Text Ð¿Ñ€Ð¸Ð¶Ð¸Ð¼Ð°ÐµÑ‚ ÐµÑ‘ Ðº Ð±Ð»Ð¸Ð¶Ð°Ð¹ÑˆÐµÐ¹ Ð´Ð¾ÑÑ‚ÑƒÐ¿Ð½Ð¾Ð¹
// ÑÑ‚ÑƒÐ¿ÐµÐ½Ð¸, Ð¿Ð¾Ñ‚Ð¾Ð¼Ñƒ Ñ‡Ñ‚Ð¾ Ð³Ð»Ð¸Ñ„Ñ‹ ÑÑ‚Ð¾ Ð±Ð¸Ñ‚Ð¼Ð°Ð¿Ñ‹ Ð¸ Ð¼Ð°ÑÑˆÑ‚Ð°Ð±Ð¸Ñ€ÑƒÑŽÑ‚ÑÑ Ñ‚Ð¾Ð»ÑŒÐºÐ¾ Ñ†ÐµÐ»Ñ‹Ð¼.

/// Ð’Ñ‹ÑÐ¾Ñ‚Ð° Ñ†Ð¸Ñ„Ñ€, Ð¿Ñ€Ð¸ ÐºÐ¾Ñ‚Ð¾Ñ€Ð¾Ð¹ ÑÑ‚Ñ€Ð¾ÐºÐ° Ð²Ð¿Ð¸ÑÑ‹Ð²Ð°ÐµÑ‚ÑÑ Ð² Ð±Ð¾ÐºÑ.
int fitPx(const char* s, int maxW, int maxH, bool bold = true) {
    return Text::capPx(Text::fit(s, maxW, maxH, bold));
}

Text::HA alignOf(const char* align) {
    if (!align || strcmp(align, "center") == 0) return Text::HA::Center;
    if (strcmp(align, "right") == 0)            return Text::HA::Right;
    return Text::HA::Left;
}

void drawBoxText(Arduino_GFX* g, const char* s, int bx, int by, int bw, int bh,
                 const char* align, VA va, int px, uint16_t color,
                 bool bold = true) {
    if (!s || !*s) return;
    g->setTextWrap(false);
    Text::drawBox(g, s, bx, by, bw, bh, alignOf(align), va,
                  Text::pick(px, bold), color);
}

void drawCenteredText(Arduino_GFX* g, const char* s, int cx, int cy,
                      int px, uint16_t color, bool bold = true) {
    if (!s || !*s) return;
    g->setTextWrap(false);
    Text::drawCentered(g, s, cx, cy, Text::pick(px, bold), color);
}

/// ÐŸÐ¾Ð»ÑƒÐ¿Ñ€Ð¾Ð·Ñ€Ð°Ñ‡Ð½Ð°Ñ Ð·Ð°Ð»Ð¸Ð²ÐºÐ° Ð¿Ð¾Ð²ÐµÑ€Ñ… Ð³Ð¾Ñ‚Ð¾Ð²Ð¾Ð³Ð¾ ÐºÐ°Ð´Ñ€Ð°.
///
/// Ð’ Arduino_GFX Ð½ÐµÑ‚ Ð°Ð»ÑŒÑ„Ð°-ÐºÐ°Ð½Ð°Ð»Ð°, Ð¿Ð¾ÑÑ‚Ð¾Ð¼Ñƒ ÑÐ¼ÐµÑˆÐ¸Ð²Ð°ÐµÐ¼ Ð¿Ð¸ÐºÑÐµÐ»Ð¸ Ñ„Ñ€ÐµÐ¹Ð¼Ð±ÑƒÑ„ÐµÑ€Ð°
/// Ð½Ð°Ð¿Ñ€ÑÐ¼ÑƒÑŽ. 217k Ð¿Ð¸ÐºÑÐµÐ»ÐµÐ¹ â€” Ð·Ð°Ð¼ÐµÑ‚Ð½Ð°Ñ Ñ†ÐµÐ½Ð°, Ð¿Ð¾ÑÑ‚Ð¾Ð¼Ñƒ Ð²Ñ‹Ð·Ñ‹Ð²Ð°ÐµÑ‚ÑÑ Ñ‚Ð¾Ð»ÑŒÐºÐ¾ ÐºÐ¾Ð³Ð´Ð°
/// Ð¾Ð²ÐµÑ€Ð»ÐµÐ¹ Ñ€ÐµÐ°Ð»ÑŒÐ½Ð¾ Ð²Ð¸Ð´ÐµÐ½ (Warning, shift-light flash).
void tintScreen(uint16_t color, float amount) {
    uint16_t* fb = Display::framebuffer();
    if (!fb) return;
    const size_t n = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT;
    for (size_t i = 0; i < n; ++i) fb[i] = blend565(fb[i], color, amount);
}

/// Ð—Ð°Ñ‚ÐµÐ¼Ð½ÐµÐ½Ð¸Ðµ ÐºÐ°Ð´Ñ€Ð° Ð½Ð° Ñ†ÐµÐ»Ð¾Ñ‡Ð¸ÑÐ»ÐµÐ½Ð½Ð¾Ð¹ Ð°Ñ€Ð¸Ñ„Ð¼ÐµÑ‚Ð¸ÐºÐµ.
///
/// keep256 â€” ÑÐºÐ¾Ð»ÑŒÐºÐ¾ ÑÑ€ÐºÐ¾ÑÑ‚Ð¸ Ð¾ÑÑ‚Ð°Ð²Ð¸Ñ‚ÑŒ, Ð² 1/256 Ð´Ð¾Ð»ÑÑ…: 179 ÑÑ‚Ð¾ Ð¿Ñ€Ð¸Ð¼ÐµÑ€Ð½Ð¾ 70%,
/// Ñ‚Ð¾ ÐµÑÑ‚ÑŒ Ð·Ð°Ñ‚ÐµÐ¼Ð½ÐµÐ½Ð¸Ðµ Ð½Ð° 30% ÐºÐ°Ðº Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ. ÐŸÐ»Ð°Ð²Ð°ÑŽÑ‰ÐµÐ¹ Ñ‚Ð¾Ñ‡ÐºÐ¸ Ð½ÐµÑ‚ ÑÐ¿ÐµÑ†Ð¸Ð°Ð»ÑŒÐ½Ð¾:
/// Ð¿Ñ€Ð¾Ñ…Ð¾Ð´ Ð¸Ð´Ñ‘Ñ‚ Ð¿Ð¾ 217k Ð¿Ð¸ÐºÑÐµÐ»ÐµÐ¹ ÐºÐ°Ð¶Ð´Ñ‹Ð¹ ÐºÐ°Ð´Ñ€, Ð¿Ð¾ÐºÐ° Ð¾Ð²ÐµÑ€Ð»ÐµÐ¹ Ð²Ð¸Ð´ÐµÐ½.
void dimScreen(uint16_t keep256) {
    uint16_t* fb = Display::framebuffer();
    if (!fb) return;
    const size_t n = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT;
    for (size_t i = 0; i < n; ++i) {
        const uint16_t c = fb[i];
        const uint16_t r = static_cast<uint16_t>((((c >> 11) & 0x1F) * keep256) >> 8);
        const uint16_t g = static_cast<uint16_t>((((c >> 5) & 0x3F) * keep256) >> 8);
        const uint16_t b = static_cast<uint16_t>(((c & 0x1F) * keep256) >> 8);
        fb[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
}

/// Ð—Ð½Ð°Ðº Ð°Ð²Ð°Ñ€Ð¸Ð¸: Ñ‚Ñ€ÐµÑƒÐ³Ð¾Ð»ÑŒÐ½Ð¸Ðº Ñ Ð²Ð¾ÑÐºÐ»Ð¸Ñ†Ð°Ñ‚ÐµÐ»ÑŒÐ½Ñ‹Ð¼ Ð·Ð½Ð°ÐºÐ¾Ð¼, Ð½Ð°Ñ€Ð¸ÑÐ¾Ð²Ð°Ð½Ð½Ñ‹Ð¹ Ð²ÐµÐºÑ‚Ð¾Ñ€Ð¾Ð¼.
///
/// Ð’ Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ Ð½Ð° ÑÑ‚Ð¾Ð¼ Ð¼ÐµÑÑ‚Ðµ ÑÑ‚Ð¾ÑÐ» emoji, Ð½Ð¾ Ð²Ð¾ Ð²ÑÑ‚Ñ€Ð¾ÐµÐ½Ð½Ñ‹Ñ… ÑˆÑ€Ð¸Ñ„Ñ‚Ð°Ñ… ÐµÐ³Ð¾ Ð½ÐµÑ‚ Ð¸
/// Ð±Ñ‹Ñ‚ÑŒ Ð½Ðµ Ð¼Ð¾Ð¶ÐµÑ‚. ÐŸÐ¾ÑÑ‚Ð¾Ð¼Ñƒ Ð¸ÐºÐ¾Ð½ÐºÐ° Ñ€Ð¸ÑÑƒÐµÑ‚ÑÑ Ð¿Ñ€Ð¸Ð¼Ð¸Ñ‚Ð¸Ð²Ð°Ð¼Ð¸ â€” Ð¸ Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€ Ð¿ÐµÑ€ÐµÐ²ÐµÐ´Ñ‘Ð½
/// Ð½Ð° Ñ‚Ð°ÐºÑƒÑŽ Ð¶Ðµ Ð¾Ñ‚Ñ€Ð¸ÑÐ¾Ð²ÐºÑƒ, Ñ‡Ñ‚Ð¾Ð±Ñ‹ Ð²Ð¸Ð´ ÑÐ¾Ð²Ð¿Ð°Ð´Ð°Ð».
void drawWarnTriangle(Arduino_GFX* g, int cx, int cy, int size, uint16_t fg, uint16_t bg) {
    const int h  = size;
    const int w  = static_cast<int>(size * 1.15f);
    const int ax = cx,          ay = cy - h / 2;
    const int lx = cx - w / 2,  ly = cy + h / 2;
    const int rx = cx + w / 2,  ry = ly;

    g->fillTriangle(ax, ay, lx, ly, rx, ry, fg);

    // Ð’Ð½ÑƒÑ‚Ñ€ÐµÐ½Ð½Ð¸Ð¹ Ñ‚Ñ€ÐµÑƒÐ³Ð¾Ð»ÑŒÐ½Ð¸Ðº Ñ†Ð²ÐµÑ‚Ð¾Ð¼ Ñ„Ð¾Ð½Ð° Ð¿Ñ€ÐµÐ²Ñ€Ð°Ñ‰Ð°ÐµÑ‚ Ñ„Ð¸Ð³ÑƒÑ€Ñƒ Ð² ÐºÐ¾Ð½Ñ‚ÑƒÑ€
    const int inset = size / 7 > 2 ? size / 7 : 2;
    g->fillTriangle(ax, ay + inset * 2, lx + inset * 2, ly - inset,
                    rx - inset * 2, ry - inset, bg);

    // Ð’Ð¾ÑÐºÐ»Ð¸Ñ†Ð°Ñ‚ÐµÐ»ÑŒÐ½Ñ‹Ð¹ Ð·Ð½Ð°Ðº
    const int barW = size / 9 > 2 ? size / 9 : 2;
    const int barH = size / 3;
    g->fillRect(cx - barW / 2, cy - barH / 3, barW, barH, fg);
    g->fillRect(cx - barW / 2, cy + barH * 2 / 3 + 2, barW, barW, fg);
}

/// Ð—Ð°Ð»Ð¸Ð²ÐºÐ° ÐºÐ¾Ð»ÑŒÑ†ÐµÐ²Ð¾Ð³Ð¾ ÑÐµÐºÑ‚Ð¾Ñ€Ð° Ñ€Ð°Ð´Ð¸Ð°Ð»ÑŒÐ½Ñ‹Ð¼Ð¸ ÑÐ¿Ð¸Ñ†Ð°Ð¼Ð¸.
///
/// Ð—Ð°Ð¼ÐµÐ½Ð° Arduino_GFX::fillArc: Ñ‚Ð¾Ñ‚ ÑÐºÐ°Ð½Ð¸Ñ€ÑƒÐµÑ‚ Ð’Ð•Ð¡Ð¬ Ð¾Ð¿Ð¸ÑÐ°Ð½Ð½Ñ‹Ð¹ ÐºÐ²Ð°Ð´Ñ€Ð°Ñ‚ Ñ
/// Ð¿Ñ€Ð¾Ð²ÐµÑ€ÐºÐ°Ð¼Ð¸ Ð½Ð° float Ð² ÐºÐ°Ð¶Ð´Ð¾Ð¼ Ð¿Ð¸ÐºÑÐµÐ»Ðµ â€” Ð´Ð»Ñ ÑˆÐºÐ°Ð»Ñ‹ 450x450 ÑÑ‚Ð¾ 203k Ð¸Ñ‚ÐµÑ€Ð°Ñ†Ð¸Ð¹
/// Ð½Ð° Ð¾Ð´Ð¸Ð½ Ð²Ñ‹Ð·Ð¾Ð², Ð¸ Ð¸Ð¼ÐµÐ½Ð½Ð¾ ÑÑ‚Ð¾ ÑÑŠÐµÐ´Ð°Ð»Ð¾ ÐºÐ°Ð´Ñ€. Ð—Ð´ÐµÑÑŒ Ñ€Ð°Ð±Ð¾Ñ‚Ð° Ð¿Ñ€Ð¾Ð¿Ð¾Ñ€Ñ†Ð¸Ð¾Ð½Ð°Ð»ÑŒÐ½Ð°
/// ÑÐ°Ð¼Ð¾Ð¹ Ð´ÑƒÐ³Ðµ: ÑˆÐ°Ð³ Ð¿Ð¾Ð´Ð¾Ð±Ñ€Ð°Ð½ Ñ‚Ð°Ðº, Ñ‡Ñ‚Ð¾Ð±Ñ‹ ÑÐ¾ÑÐµÐ´Ð½Ð¸Ðµ ÑÐ¿Ð¸Ñ†Ñ‹ ÑÐ¼Ñ‹ÐºÐ°Ð»Ð¸ÑÑŒ Ð¿Ð¾ Ð²Ð½ÐµÑˆÐ½ÐµÐ¼Ñƒ
/// Ñ€Ð°Ð´Ð¸ÑƒÑÑƒ, Ñ‚Ð¾ ÐµÑÑ‚ÑŒ Ð¿Ñ€Ð¸Ð¼ÐµÑ€Ð½Ð¾ Ð´Ð»Ð¸Ð½Ð°_Ð´ÑƒÐ³Ð¸ * Ñ‚Ð¾Ð»Ñ‰Ð¸Ð½Ð° Ð¿Ð¸ÐºÑÐµÐ»ÐµÐ¹.
void fillArcSpokes(Arduino_GFX* g, int cx, int cy, int rOuter, int rInner,
                   float aStart, float aEnd, uint16_t color) {
    if (rOuter <= rInner || rOuter <= 0) return;

    const float sweep = aEnd - aStart;
    if (fabsf(sweep) < 0.05f) return;

    // Ð”Ð»Ð¸Ð½Ð° Ð²Ð½ÐµÑˆÐ½ÐµÐ¹ Ð´ÑƒÐ³Ð¸ Ð² Ð¿Ð¸ÐºÑÐµÐ»ÑÑ… = ÑˆÐ°Ð³ Ð² Ð¾Ð´Ð¸Ð½ Ð¿Ð¸ÐºÑÐµÐ»ÑŒ
    const int steps = static_cast<int>(ceilf(fabsf(sweep) * DEG_TO_RAD * rOuter));
    const int n = steps < 1 ? 1 : steps;

    for (int i = 0; i <= n; ++i) {
        const float a = (aStart + sweep * i / n) * DEG_TO_RAD;
        const float ca = cosf(a), sa = sinf(a);
        g->drawLine(cx + static_cast<int>(ca * rInner), cy + static_cast<int>(sa * rInner),
                    cx + static_cast<int>(ca * rOuter), cy + static_cast<int>(sa * rOuter),
                    color);
    }
}

// â”€â”€â”€ Ð¡Ð¾ÑÑ‚Ð¾ÑÐ½Ð¸Ðµ Ð²Ð¸Ð´Ð¶ÐµÑ‚Ð¾Ð² Ð¼ÐµÐ¶Ð´Ñƒ ÐºÐ°Ð´Ñ€Ð°Ð¼Ð¸ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

constexpr uint8_t  kWarnPool     = 6;
constexpr uint8_t  kGraphPool    = 3;
constexpr uint8_t  kGraphSignals = 4;
constexpr uint16_t kGraphPoints  = 120;
constexpr uint8_t  kIdLen        = 24;
constexpr uint8_t  kTrailPool    = 2;
constexpr uint8_t  kTrailLen     = 30;

struct WarnState {
    char     id[kIdLen];
    bool     used;
    uint32_t showUntilMs;
};
WarnState s_warn[kWarnPool] = {};

struct GraphState {
    char     id[kIdLen];
    bool     used;
    uint16_t head;
    uint16_t filled;
    uint32_t lastMs;
    float    v[kGraphSignals][kGraphPoints];
};
GraphState s_graph[kGraphPool] = {};

WarnState* warnState(const char* id) {
    for (auto& w : s_warn) if (w.used && strncmp(w.id, id, kIdLen) == 0) return &w;
    for (auto& w : s_warn) {
        if (!w.used) {
            w.used = true;
            strncpy(w.id, id, kIdLen - 1);
            w.id[kIdLen - 1] = '\0';
            w.showUntilMs = 0;
            return &w;
        }
    }
    return nullptr;
}

/// Ð¡Ð»ÐµÐ´ ÑˆÐ°Ñ€Ð¸ÐºÐ° G-force: ÐºÐ¾Ð»ÑŒÑ†ÐµÐ²Ð¾Ð¹ Ð±ÑƒÑ„ÐµÑ€ Ð¿Ð¾ÑÐ»ÐµÐ´Ð½Ð¸Ñ… Ð¿Ð¾Ð·Ð¸Ñ†Ð¸Ð¹.
struct TrailState {
    char    id[kIdLen];
    bool    used;
    uint8_t head;
    uint8_t filled;
    int16_t x[kTrailLen];
    int16_t y[kTrailLen];
};
TrailState s_trail[kTrailPool] = {};

TrailState* trailState(const char* id) {
    for (auto& t : s_trail) if (t.used && strncmp(t.id, id, kIdLen) == 0) return &t;
    for (auto& t : s_trail) {
        if (!t.used) {
            t.used = true;
            strncpy(t.id, id, kIdLen - 1);
            t.id[kIdLen - 1] = '\0';
            t.head = 0;
            t.filled = 0;
            return &t;
        }
    }
    return nullptr;
}

GraphState* graphState(const char* id) {
    for (auto& g : s_graph) if (g.used && strncmp(g.id, id, kIdLen) == 0) return &g;
    for (auto& g : s_graph) {
        if (!g.used) {
            g.used = true;
            strncpy(g.id, id, kIdLen - 1);
            g.id[kIdLen - 1] = '\0';
            g.head = 0;
            g.filled = 0;
            g.lastMs = 0;
            return &g;
        }
    }
    return nullptr;
}

// â”€â”€â”€ arc_gauge â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintArcGauge(const Frame& f, JsonObjectConst w, const R& r, float value) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const float mn = pF(p, "min", 0.0f);
    const float mx = pF(p, "max", 100.0f);
    if (mx - mn < 1.0e-6f) return;

    const float a0   = pF(p, "startAngle", 135.0f);
    const float a1   = pF(p, "endAngle",   405.0f);
    const float span = a1 - a0;

    const int outer = (r.w < r.h ? r.w : r.h) / 2;
    const int thick = pI(p, "thickness", 16);
    const int inner = (outer - thick) > 1 ? (outer - thick) : 1;
    const int cx = r.x + r.w / 2;
    const int cy = r.y + r.h / 2;

    const uint16_t base = pC(p, "color", Layout::themeFg());
    const uint16_t trk  = pC(p, "trackColor", 0x18E3);

    fillArcSpokes(g, cx, cy, outer, inner, a0, a1, trk);

    // Ð—Ð¾Ð½Ñ‹ Ð¿Ð¾Ð´ÐºÑ€Ð°ÑˆÐ¸Ð²Ð°ÑŽÑ‚ Ð´Ð¾Ñ€Ð¾Ð¶ÐºÑƒ
    JsonArrayConst zones = p["zones"];
    for (JsonObjectConst z : zones) {
        const float zf = z["from"] | mn;
        const float zt = z["to"]   | mx;
        const float zs = a0 + span * clampf((zf - mn) / (mx - mn), 0.0f, 1.0f);
        const float ze = a0 + span * clampf((zt - mn) / (mx - mn), 0.0f, 1.0f);
        fillArcSpokes(g, cx, cy, outer, inner, zs, ze,
                      rgb565FromHex(z["color"] | static_cast<const char*>(nullptr), base));
    }

    // Ð”ÑƒÐ³Ð° Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ñ. Ð¦Ð²ÐµÑ‚ Ð±ÐµÑ€Ñ‘Ñ‚ÑÑ Ð¸Ð· Ð·Ð¾Ð½Ñ‹, Ð² ÐºÐ¾Ñ‚Ð¾Ñ€Ð¾Ð¹ Ð½Ð°Ñ…Ð¾Ð´Ð¸Ñ‚ÑÑ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ:
    // Ð½Ð° Ð¾Ñ‚ÑÐµÑ‡ÐºÐµ Ð²ÑÑ Ð´ÑƒÐ³Ð° ÑÑ‚Ð°Ð½Ð¾Ð²Ð¸Ñ‚ÑÑ ÐºÑ€Ð°ÑÐ½Ð¾Ð¹, ÑÑ‚Ð¾ Ñ‡Ð¸Ñ‚Ð°ÐµÑ‚ÑÑ Ð¼Ð³Ð½Ð¾Ð²ÐµÐ½Ð½Ð¾.
    const float n  = clampf((value - mn) / (mx - mn), 0.0f, 1.0f);
    fillArcSpokes(g, cx, cy, outer, inner, a0, a0 + span * n, zoneColor(zones, value, base));

    // Ð Ð¸ÑÐºÐ¸ Ð¸ Ð¿Ð¾Ð´Ð¿Ð¸ÑÐ¸
    JsonObjectConst t = p["ticks"];
    const float major = t["major"] | 0.0f;
    if (major > 0.0f) {
        const bool  labels = t["labels"] | false;
        const float div    = t["labelDivisor"] | 1.0f;
        const uint16_t mut = Layout::themeMuted();

        for (float v = mn; v <= mx + 0.001f; v += major) {
            const float rad = (a0 + span * (v - mn) / (mx - mn)) * DEG_TO_RAD;
            const float ca = cosf(rad), sa = sinf(rad);

            const int t1 = inner - 2;
            const int t2 = inner - 9;
            g->drawLine(cx + static_cast<int>(ca * t1), cy + static_cast<int>(sa * t1),
                        cx + static_cast<int>(ca * t2), cy + static_cast<int>(sa * t2), mut);

            if (labels) {
                char buf[12];
                snprintf(buf, sizeof buf, "%g", v / (div > 0.0f ? div : 1.0f));
                const int lr = inner - 22;
                drawCenteredText(g, buf,
                                 cx + static_cast<int>(ca * lr),
                                 cy + static_cast<int>(sa * lr), 13, mut, false);
            }
        }
    }
}

// â”€â”€â”€ numeric â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintNumeric(const Frame& f, JsonObjectConst w, const R& r, float value) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const int   decimals = pI(p, "decimals", 0);
    const char* align    = pS(p, "align", "center");
    const char* caption  = pS(p, "caption", "");
    const char* unit     = w["unit"] | "";
    const bool  showUnit = pB(p, "showUnit", true);

    uint16_t col = pC(p, "color", Layout::themeFg());
    if (pB(p, "colorFromZones", false)) col = zoneColor(p["zones"], value, col);

    char val[24];
    snprintf(val, sizeof val, "%.*f", decimals, static_cast<double>(value));

    const bool hasCap  = caption && *caption;
    const bool hasUnit = showUnit && unit && *unit;

    // Ð Ð°Ð·Ð¼ÐµÑ€ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ñ: ÑÐ²Ð½Ñ‹Ð¹ fontSize Ð»Ð¸Ð±Ð¾ Ð°Ð²Ñ‚Ð¾Ð¿Ð¾Ð´Ð³Ð¾Ð½ÐºÐ° â€” ÐºÐ°Ðº Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ
    const float fontSize = pF(p, "fontSize", 0.0f);
    int vs;
    if (fontSize > 0.0f) {
        vs = Text::capFromEm(fontSize);
    } else {
        const int capBand = hasCap  ? static_cast<int>(r.h * 0.22f) : 0;
        const int uniBand = hasUnit ? static_cast<int>(r.h * 0.20f) : 0;
        const int availH  = r.h - capBand - uniBand;
        vs = fitPx(val, static_cast<int>(r.w * 0.94f), availH > 1 ? availH : 1);
    }

    // ÐŸÐ¾Ð´Ð¿Ð¸ÑÐ¸ Ð¿Ñ€Ð¾Ð¿Ð¾Ñ€Ñ†Ð¸Ð¾Ð½Ð°Ð»ÑŒÐ½Ñ‹ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸ÑŽ, Ñ‡Ñ‚Ð¾Ð±Ñ‹ Ð²Ð¸Ð´Ð¶ÐµÑ‚ Ñ€Ð¾Ñ Ñ†ÐµÐ»Ð¸ÐºÐ¾Ð¼
    const int      ss   = vs * 30 / 100 < 10 ? 10 : vs * 30 / 100;
    const int      capH = hasCap  ? ss + 4 : 0;
    const int      uniH = hasUnit ? ss + 4 : 0;
    const uint16_t mut  = Layout::themeMuted();

    if (hasCap) {
        drawBoxText(g, caption, r.x, r.y, r.w, capH, align, VA::Top, ss, mut, false);
    }
    drawBoxText(g, val, r.x, r.y + capH, r.w, r.h - capH - uniH, align, VA::Mid, vs, col);
    if (hasUnit) {
        drawBoxText(g, unit, r.x, r.y + r.h - uniH, r.w, uniH, align, VA::Bottom, ss, mut, false);
    }
}

// â”€â”€â”€ label â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintLabel(const Frame& f, JsonObjectConst w, const R& r) {
    JsonObjectConst p = w["props"];
    const char* text  = pS(p, "text", "");
    const char* align = pS(p, "align", "center");

    const float fontSize = pF(p, "fontSize", 0.0f);
    const int sz = fontSize > 0.0f
        ? Text::capFromEm(fontSize)
        : fitPx(text, static_cast<int>(r.w * 0.94f), static_cast<int>(r.h * 0.72f));

    drawBoxText(f.gfx, text, r.x, r.y, r.w, r.h, align, VA::Mid, sz,
                pC(p, "color", Layout::themeFg()));
}

// â”€â”€â”€ bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintBar(const Frame& f, JsonObjectConst w, const R& r, float value) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const float mn = pF(p, "min", 0.0f);
    const float mx = pF(p, "max", 100.0f);
    const float n  = (mx - mn) > 1.0e-6f ? clampf((value - mn) / (mx - mn), 0.0f, 1.0f) : 0.0f;

    const bool vertical = strcmp(pS(p, "orientation", "horizontal"), "vertical") == 0;
    const int  rad      = pI(p, "radius", 4);

    g->fillRoundRect(r.x, r.y, r.w, r.h, rad, pC(p, "trackColor", 0x18E3));

    const uint16_t col = zoneColor(p["zones"], value, pC(p, "color", Layout::themeFg()));
    if (vertical) {
        const int fh = static_cast<int>(r.h * n);
        if (fh > 0) g->fillRoundRect(r.x, r.y + r.h - fh, r.w, fh, rad > fh / 2 ? fh / 2 : rad, col);
    } else {
        const int fw = static_cast<int>(r.w * n);
        if (fw > 0) g->fillRoundRect(r.x, r.y, fw, r.h, rad > fw / 2 ? fw / 2 : rad, col);
    }
}

// â”€â”€â”€ steering â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintSteering(const Frame& f, JsonObjectConst w, const R& r, float pos) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    // -1 Ð²Ð»ÐµÐ²Ð¾ â€¦ 0 Ñ†ÐµÐ½Ñ‚Ñ€ â€¦ +1 Ð²Ð¿Ñ€Ð°Ð²Ð¾
    float n = clampf((pos - 0.5f) * 2.0f, -1.0f, 1.0f);

    // Ð—Ð¾Ð½Ð° Ð½ÐµÑ‡ÑƒÐ²ÑÑ‚Ð²Ð¸Ñ‚ÐµÐ»ÑŒÐ½Ð¾ÑÑ‚Ð¸: Ð¾ÑÑ‚Ð°Ñ‚Ð¾Ðº Ð´Ð¸Ð°Ð¿Ð°Ð·Ð¾Ð½Ð° Ñ€Ð°ÑÑ‚ÑÐ³Ð¸Ð²Ð°ÐµÑ‚ÑÑ Ð¾Ð±Ñ€Ð°Ñ‚Ð½Ð¾ Ð½Ð° 0..1,
    // Ð¸Ð½Ð°Ñ‡Ðµ Ð¿Ð¾Ð»Ð¾ÑÐ° Ñ‚ÐµÑ€ÑÐ»Ð° Ð±Ñ‹ Ñ‡Ð°ÑÑ‚ÑŒ Ñ…Ð¾Ð´Ð° Ð¸ Ð½Ðµ Ð´Ð¾Ñ…Ð¾Ð´Ð¸Ð»Ð° Ð´Ð¾ ÐºÑ€Ð°Ñ
    const float dz = pF(p, "deadzone", 0.0f);
    if (dz > 0.0f && dz < 1.0f) {
        const float mag = fabsf(n);
        n = mag <= dz ? 0.0f : (n < 0.0f ? -1.0f : 1.0f) * ((mag - dz) / (1.0f - dz));
    }

    const bool showValue = pB(p, "showValue", false);
    const int  textH = showValue ? (r.h * 0.34f < 16.0f ? static_cast<int>(r.h * 0.34f) : 16) : 0;
    const int  barH  = (r.h - textH) > 2 ? (r.h - textH) : 2;
    const int  halfW = r.w / 2;
    const int  rad   = pI(p, "radius", 4) > barH / 2 ? barH / 2 : pI(p, "radius", 4);

    g->fillRoundRect(r.x, r.y, r.w, barH, rad, pC(p, "trackColor", 0x18E3));

    const int len = static_cast<int>(fabsf(n) * halfW);
    if (len > 0) {
        const int x = n >= 0.0f ? r.x + halfW : r.x + halfW - len;
        g->fillRoundRect(x, r.y, len, barH, rad > len / 2 ? len / 2 : rad,
                         pC(p, "color", Layout::themeFg()));
    }

    // Ð Ð¸ÑÐºÐ° Ñ†ÐµÐ½Ñ‚Ñ€Ð° Ð¿Ð¾Ð²ÐµÑ€Ñ… Ð·Ð°Ð¿Ð¾Ð»Ð½ÐµÐ½Ð¸Ñ â€” Ð½Ð¾Ð»ÑŒ Ð²Ð¸Ð´ÐµÐ½ Ð¿Ñ€Ð¸ Ð»ÑŽÐ±Ð¾Ð¼ Ð¾Ñ‚ÐºÐ»Ð¾Ð½ÐµÐ½Ð¸Ð¸
    if (pB(p, "centerMark", true)) {
        g->fillRect(r.x + halfW - 1, r.y, 2, barH, 0xBDF7);
    }

    if (showValue) {
        const int deg = static_cast<int>(lroundf(n * pF(p, "maxAngle", 450.0f)));
        char buf[16];
        if (deg == 0) snprintf(buf, sizeof buf, "0");
        else          snprintf(buf, sizeof buf, "%c%d", deg > 0 ? '+' : '-', abs(deg));
        drawBoxText(g, buf, r.x, r.y + barH, r.w, textH, "center", VA::Bottom, 13,
                    Layout::themeMuted(), false);
    }
}

// â”€â”€â”€ shift_light â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintShiftLight(const Frame& f, JsonObjectConst w, const R& r, float rpm) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    JsonArrayConst stages = p["stages"];
    const int n = static_cast<int>(stages.size());
    if (n <= 0) return;

    const char* mode = pS(p, "mode", "segments");

    // ÐœÐ¸Ð³Ð°Ð½Ð¸Ðµ Ð¿Ð¾ÑÐ»ÐµÐ´Ð½ÐµÐ¹ ÑÑ‚ÑƒÐ¿ÐµÐ½Ð¸ Ð½Ð° Ð¾Ñ‚ÑÐµÑ‡ÐºÐµ
    auto blinkOn = [&](JsonObjectConst st) -> bool {
        const float hz = st["blinkHz"] | 0.0f;
        if (hz <= 0.0f) return true;
        return fmodf(f.timeSec * hz, 1.0f) < 0.5f;
    };

    if (strcmp(mode, "flash") == 0) {
        // ÐŸÐ¾Ð»Ð½Ð¾ÑÐºÑ€Ð°Ð½Ð½Ð°Ñ Ð²ÑÐ¿Ñ‹ÑˆÐºÐ° Ð¿Ñ€Ð¸ Ð´Ð¾ÑÑ‚Ð¸Ð¶ÐµÐ½Ð¸Ð¸ Ð¿Ð¾ÑÐ»ÐµÐ´Ð½ÐµÐ¹ ÑÑ‚ÑƒÐ¿ÐµÐ½Ð¸
        JsonObjectConst last = stages[n - 1];
        if (rpm >= (last["at"] | 0.0f) && blinkOn(last)) {
            tintScreen(rgb565FromHex(last["color"] | static_cast<const char*>(nullptr), 0xF800), 0.45f);
        }
        return;
    }

    if (strcmp(mode, "arc") == 0) {
        // Ð¢Ð¾Ñ‡ÐºÐ¸ Ð¿Ð¾ Ð´ÑƒÐ³Ðµ Ð²Ð½ÑƒÑ‚Ñ€Ð¸ Ñ€Ð°Ð¼ÐºÐ¸
        const int dotSize = pI(p, "dotSize", 0);
        const int dr = dotSize > 0 ? dotSize / 2 : (r.h < r.w / (2 * n) ? r.h / 2 : r.w / (4 * n));
        const int rx = r.w / 2 - dr;
        const int ry = r.h - dr;
        const int cx = r.x + r.w / 2;
        const int cy = r.y + r.h - dr;

        for (int i = 0; i < n; ++i) {
            JsonObjectConst st = stages[i];
            const bool lit = rpm >= (st["at"] | 0.0f) && blinkOn(st);
            const float a = PI + PI * (n == 1 ? 0.5f : static_cast<float>(i) / (n - 1));
            const int dx = cx + static_cast<int>(cosf(a) * rx);
            const int dy = cy + static_cast<int>(sinf(a) * ry);
            g->fillCircle(dx, dy, dr > 1 ? dr : 1,
                          lit ? rgb565FromHex(st["color"] | static_cast<const char*>(nullptr), 0xFFFF)
                              : 0x18E3);
        }
        return;
    }

    // segments (Ð¿Ð¾ ÑƒÐ¼Ð¾Ð»Ñ‡Ð°Ð½Ð¸ÑŽ)
    const int gap  = 3;
    const int segW = (r.w - gap * (n - 1)) / n;
    if (segW <= 0) return;

    for (int i = 0; i < n; ++i) {
        JsonObjectConst st = stages[i];
        const bool lit = rpm >= (st["at"] | 0.0f) && blinkOn(st);
        g->fillRoundRect(r.x + i * (segW + gap), r.y, segW, r.h, 2,
                         lit ? rgb565FromHex(st["color"] | static_cast<const char*>(nullptr), 0xFFFF)
                             : 0x18E3);
    }
}

// â”€â”€â”€ warning â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintWarning(const Frame& f, JsonObjectConst w, const R& /*r*/) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const char* id = w["id"] | "warn";
    WarnState* st = warnState(id);
    if (!st) return;

    // Ð£ÑÐ»Ð¾Ð²Ð¸Ðµ ÑÑ€Ð°Ð±Ð°Ñ‚Ñ‹Ð²Ð°Ð½Ð¸Ñ: Ð¿Ð¾Ñ€Ð¾Ð³Ð¸ Ð¸Ð· Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ð° Ð»Ð¸Ð±Ð¾ when{} Ð¸Ð· ÑÑ…ÐµÐ¼Ñ‹
    bool triggered = false;
    float shown = 0.0f;

    const char* sigId = w["signal"] | static_cast<const char*>(nullptr);
    JsonObjectConst when = p["when"];
    if (!when.isNull()) {
        const char* ws = when["signal"] | static_cast<const char*>(nullptr);
        if (ws) {
            const float v   = Signals::get(ws);
            const float ref = when["value"] | 0.0f;
            const char* cmp = when["cmp"] | "<";
            triggered = (strcmp(cmp, ">") == 0) ? (v > ref) : (v < ref);
            shown = v;
        }
    } else if (sigId) {
        const float v = Signals::get(sigId);
        shown = v;
        if (p["triggerBelow"].is<float>() && v < (p["triggerBelow"] | 0.0f)) triggered = true;
        if (p["triggerAbove"].is<float>() && v > (p["triggerAbove"] | 0.0f)) triggered = true;
    }

    const uint32_t now = millis();
    if (triggered) st->showUntilMs = now + 2000;   // Ð°Ð²Ñ‚Ð¾Ð¿Ñ€Ð¾Ð¿Ð°Ð´Ð°Ð½Ð¸Ðµ Ñ‡ÐµÑ€ÐµÐ· 2 Ñ
    if (now >= st->showUntilMs) return;

    // ÐÐ²Ð°Ñ€Ð¸Ð¹Ð½Ð¾Ðµ ÑÐ¾Ð¾Ð±Ñ‰ÐµÐ½Ð¸Ðµ â€” Ð¾Ð²ÐµÑ€Ð»ÐµÐ¹ Ð²ÑÐµÐ³Ð¾ ÑÐºÑ€Ð°Ð½Ð°, Ð° Ð½Ðµ ÑÐ¾Ð´ÐµÑ€Ð¶Ð¸Ð¼Ð¾Ðµ Ñ€Ð°Ð¼ÐºÐ¸.
    //
    // rect ÑƒÐ¼Ñ‹ÑˆÐ»ÐµÐ½Ð½Ð¾ Ð¸Ð³Ð½Ð¾Ñ€Ð¸Ñ€ÑƒÐµÑ‚ÑÑ: Ð¿Ñ€ÐµÐ´ÑƒÐ¿Ñ€ÐµÐ¶Ð´ÐµÐ½Ð¸Ðµ Ð¾ Ð¿Ð°Ð´ÐµÐ½Ð¸Ð¸ Ð´Ð°Ð²Ð»ÐµÐ½Ð¸Ñ Ð¼Ð°ÑÐ»Ð°
    // Ð´Ð¾Ð»Ð¶Ð½Ð¾ Ñ‡Ð¸Ñ‚Ð°Ñ‚ÑŒÑÑ Ð¼Ð³Ð½Ð¾Ð²ÐµÐ½Ð½Ð¾ Ð¸ Ð½Ðµ Ð¼Ð¾Ð¶ÐµÑ‚ Ð·Ð°Ð²Ð¸ÑÐµÑ‚ÑŒ Ð¾Ñ‚ Ñ‚Ð¾Ð³Ð¾, Ð² ÐºÐ°ÐºÐ¾Ð¹ ÑƒÐ³Ð¾Ð» ÐµÐ³Ð¾
    // Ð¿Ð¾Ð»Ð¾Ð¶Ð¸Ð»Ð¸ Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ. Ð ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€ Ñ€Ð¸ÑÑƒÐµÑ‚ ÐµÐ³Ð¾ Ñ‚Ð°Ðº Ð¶Ðµ.
    const uint16_t col = pC(p, "color", Layout::themeCrit());
    dimScreen(179);   // Ð¾ÑÑ‚Ð°Ð²Ð¸Ñ‚ÑŒ 70% ÑÑ€ÐºÐ¾ÑÑ‚Ð¸ = Ð·Ð°Ñ‚ÐµÐ¼Ð½ÐµÐ½Ð¸Ðµ Ð½Ð° 30%

    const int cx = LCD_WIDTH / 2;
    const int cy = LCD_HEIGHT / 2;
    const int rr = static_cast<int>((LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) * 0.28f);

    // Ð Ð°Ð´Ð¸Ð°Ð»ÑŒÐ½Ñ‹Ð¹ Ð³Ñ€Ð°Ð´Ð¸ÐµÐ½Ñ‚: Ð¾Ñ‚ Ð½Ð°ÑÑ‹Ñ‰ÐµÐ½Ð½Ð¾Ð³Ð¾ Ñ†ÐµÐ½Ñ‚Ñ€Ð° Ðº Ð¿Ð¾Ñ‡Ñ‚Ð¸ Ð¿Ñ€Ð¾Ð·Ñ€Ð°Ñ‡Ð½Ð¾Ð¼Ñƒ ÐºÑ€Ð°ÑŽ.
    // ÐžÐ´Ð¸Ð½ Ð¿Ñ€Ð¾Ñ…Ð¾Ð´ ÐºÐ¾Ð½Ñ†ÐµÐ½Ñ‚Ñ€Ð¸Ñ‡ÐµÑÐºÐ¸Ð¼Ð¸ Ð¾ÐºÑ€ÑƒÐ¶Ð½Ð¾ÑÑ‚ÑÐ¼Ð¸ Ð¿Ð¾Ð²Ñ‚Ð¾Ñ€ÑÐµÑ‚ createRadialGradient
    // Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ð° Ð¸ ÑÑ‚Ð¾Ð¸Ñ‚ Ð´ÐµÑˆÐµÐ²Ð»Ðµ ÑÐ¼ÐµÑˆÐ¸Ð²Ð°Ð½Ð¸Ñ ÐºÐ°Ð¶Ð´Ð¾Ð³Ð¾ Ð¿Ð¸ÐºÑÐµÐ»Ñ.
    const uint16_t bg = f.bg;
    for (int rad = rr; rad >= 0; --rad) {
        const float k = static_cast<float>(rad) / rr;   // 0 Ð² Ñ†ÐµÐ½Ñ‚Ñ€Ðµ, 1 Ð½Ð° ÐºÑ€Ð°ÑŽ
        g->drawCircle(cx, cy, rad, blend565(bg, col, 0.85f - 0.65f * k));
    }

    const char* label    = pS(p, "label", "WARNING");
    const bool  hasLabel = label && *label;

    drawWarnTriangle(g, cx, cy - static_cast<int>(rr * (hasLabel ? 0.22f : 0.05f)),
                     static_cast<int>(rr * 0.62f), 0xFFFF, blend565(bg, col, 0.80f));

    if (hasLabel) {
        drawBoxText(g, label, cx - rr, cy + static_cast<int>(rr * 0.20f),
                    rr * 2, static_cast<int>(rr * 0.30f), "center", VA::Top,
                    static_cast<int>(rr * 0.28f), 0xFFFF);
    }

    const char* unit = w["unit"] | "";
    char buf[24];
    snprintf(buf, sizeof buf, "%.*f%s%s",
             strcmp(unit, "bar") == 0 ? 2 : (shown > 100.0f ? 0 : 1),
             static_cast<double>(shown), *unit ? " " : "", unit);
    drawBoxText(g, buf, cx - rr, cy + static_cast<int>(rr * (hasLabel ? 0.50f : 0.32f)),
                rr * 2, static_cast<int>(rr * 0.26f), "center", VA::Top,
                static_cast<int>(rr * 0.22f), 0xFFFF, false);
}

// â”€â”€â”€ gforce â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintGForce(const Frame& f, JsonObjectConst w, const R& r) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const float range = pF(p, "range", 2.0f);
    const int   rings = pI(p, "rings", 2);
    if (range <= 0.0f || rings < 1) return;

    // Ð Ð°Ð´Ð°Ñ€ Ð·Ð°Ð½Ð¸Ð¼Ð°ÐµÑ‚ Ð²ÐµÑ€Ñ…Ð½Ð¸Ðµ ~80% Ð²Ñ‹ÑÐ¾Ñ‚Ñ‹, ÑÐ½Ð¸Ð·Ñƒ Ð¾ÑÑ‚Ð°Ñ‘Ñ‚ÑÑ Ð¿Ð¾Ð»Ð¾ÑÐ° Ð¿Ð¾Ð´ Ñ†Ð¸Ñ„Ñ€Ñ‹ â€”
    // Ñ€Ð¾Ð²Ð½Ð¾ Ñ‚Ð° Ð¶Ðµ Ñ€Ð°ÑÐºÐ»Ð°Ð´ÐºÐ°, Ñ‡Ñ‚Ð¾ Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ
    const int radarSize = r.w < static_cast<int>(r.h * 0.80f)
                            ? r.w : static_cast<int>(r.h * 0.80f);
    const int R0 = radarSize / 2;
    if (R0 < 8) return;

    const int cx = r.x + r.w / 2;
    const int cy = r.y + R0 + static_cast<int>(r.h * 0.04f);

    const uint16_t bg  = f.bg;
    const uint16_t fg  = Layout::themeFg();
    const uint16_t mut = Layout::themeMuted();

    // ÐŸÐ¾Ð´Ð»Ð¾Ð¶ÐºÐ° Ñ€Ð°Ð´Ð°Ñ€Ð° Ð¸ ÐµÐ³Ð¾ Ð¾Ð±Ð²Ð¾Ð´ÐºÐ°
    g->fillCircle(cx, cy, R0, blend565(bg, 0xFFFF, 0.04f));
    g->drawCircle(cx, cy, R0, blend565(bg, 0xFFFF, 0.15f));

    // ÐšÐ¾Ð»ÑŒÑ†Ð° Ñ Ð¿Ð¾Ð´Ð¿Ð¸ÑÑÐ¼Ð¸ Ð² G
    const uint16_t ringCol = blend565(bg, 0xFFFF, 0.10f);
    for (int i = 1; i <= rings; ++i) {
        const int ringR = R0 * i / rings;
        g->drawCircle(cx, cy, ringR, ringCol);

        char lab[8];
        snprintf(lab, sizeof lab, "%.1f", static_cast<double>(range * i / rings));
        drawCenteredText(g, lab,
                         cx + static_cast<int>(ringR * 0.72f),
                         cy - static_cast<int>(ringR * 0.72f),
                         R0 * 11 / 100 < 10 ? 10 : R0 * 11 / 100,
                         blend565(bg, 0xFFFF, 0.25f), false);
    }

    // ÐšÑ€ÐµÑÑ‚Ð¾Ð²Ð¸Ð½Ð° Ð¿ÑƒÐ½ÐºÑ‚Ð¸Ñ€Ð¾Ð¼: Ð² Arduino_GFX Ð½ÐµÑ‚ setLineDash, Ð¿Ð¾ÑÑ‚Ð¾Ð¼Ñƒ ÑˆÑ‚Ñ€Ð¸Ñ…Ð¸
    // Ð²Ñ‹ÐºÐ»Ð°Ð´Ñ‹Ð²Ð°ÑŽÑ‚ÑÑ Ð²Ñ€ÑƒÑ‡Ð½ÑƒÑŽ Ñ Ñ‚ÐµÐ¼ Ð¶Ðµ ÑˆÐ°Ð³Ð¾Ð¼ 3/5, Ñ‡Ñ‚Ð¾ Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ
    const uint16_t crossCol = blend565(bg, 0xFFFF, 0.12f);
    for (int d = -R0; d <= R0; d += 8) {
        g->drawFastHLine(cx + d, cy, 3, crossCol);
        g->drawFastVLine(cx, cy + d, 3, crossCol);
    }

    const int axisPx = R0 * 10 / 100 < 9 ? 9 : R0 * 10 / 100;
    const uint16_t axisCol = blend565(bg, 0xFFFF, 0.20f);
    drawCenteredText(g, "BRAKE", cx, cy - R0 + axisPx, axisPx, axisCol, false);
    drawCenteredText(g, "ACCEL", cx, cy + R0 - axisPx, axisPx, axisCol, false);

    const float ax = Signals::get(p["signalX"] | "imu.ax");
    const float ay = Signals::get(p["signalY"] | "imu.ay");

    // Ð˜Ð½ÐµÑ€Ñ†Ð¸Ñ, Ð° Ð½Ðµ ÑƒÑÐºÐ¾Ñ€ÐµÐ½Ð¸Ðµ: Ð°ÐºÑÐµÐ»ÐµÑ€Ð¾Ð¼ÐµÑ‚Ñ€ Ð´Ð°Ñ‘Ñ‚ ÑƒÑÐºÐ¾Ñ€ÐµÐ½Ð¸Ðµ ÐºÑƒÐ·Ð¾Ð²Ð°, Ð° Ð²Ð¾Ð´Ð¸Ñ‚ÐµÐ»ÑŒ
    // Ð¾Ñ‰ÑƒÑ‰Ð°ÐµÑ‚ ÑÐ¸Ð»Ñƒ Ð² Ð¿Ñ€Ð¾Ñ‚Ð¸Ð²Ð¾Ð¿Ð¾Ð»Ð¾Ð¶Ð½ÑƒÑŽ ÑÑ‚Ð¾Ñ€Ð¾Ð½Ñƒ. Ð Ð°Ð·Ð³Ð¾Ð½ ÑƒÐ²Ð¾Ð´Ð¸Ñ‚ ÑˆÐ°Ñ€Ð¸Ðº Ð²Ð½Ð¸Ð·,
    // Ð¿Ð¾Ð²Ð¾Ñ€Ð¾Ñ‚ Ð²Ð¿Ñ€Ð°Ð²Ð¾ â€” Ð²Ð»ÐµÐ²Ð¾. Ð—Ð½Ð°ÐºÐ¸ Ð¸Ð½Ð²ÐµÑ€Ñ‚Ð¸Ñ€Ð¾Ð²Ð°Ð½Ñ‹, ÐºÐ°Ðº Ð² Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ðµ.
    const int bx = cx - static_cast<int>(clampf(ax / range, -1.0f, 1.0f) * R0);
    const int by = cy + static_cast<int>(clampf(ay / range, -1.0f, 1.0f) * R0);

    // Ð¡Ð»ÐµÐ´: Ñ‡ÐµÐ¼ ÑÐ²ÐµÐ¶ÐµÐµ Ñ‚Ð¾Ñ‡ÐºÐ°, Ñ‚ÐµÐ¼ ÐºÑ€ÑƒÐ¿Ð½ÐµÐµ Ð¸ ÑÑ€Ñ‡Ðµ
    if (pB(p, "trail", true)) {
        if (TrailState* ts = trailState(w["id"] | "gforce")) {
            ts->x[ts->head] = static_cast<int16_t>(bx);
            ts->y[ts->head] = static_cast<int16_t>(by);
            ts->head = (ts->head + 1) % kTrailLen;
            if (ts->filled < kTrailLen) ++ts->filled;

            for (uint8_t i = 0; i < ts->filled; ++i) {
                const uint8_t idx = (ts->head + kTrailLen - ts->filled + i) % kTrailLen;
                const float   age = static_cast<float>(i) / ts->filled;   // 0 ÑÑ‚Ð°Ñ€Ñ‹Ð¹ â€¦ 1 ÑÐ²ÐµÐ¶Ð¸Ð¹
                const int     tr  = static_cast<int>(4.0f * age) < 2 ? 2 : static_cast<int>(4.0f * age);
                g->fillCircle(ts->x[idx], ts->y[idx], tr,
                              blend565(bg, 0x055F, age * 0.5f));
            }
        }
    }

    // Ð¨Ð°Ñ€Ð¸Ðº: Ð²Ð¼ÐµÑÑ‚Ð¾ Ð³Ñ€Ð°Ð´Ð¸ÐµÐ½Ñ‚Ð° Ð¸ ÑÐ²ÐµÑ‡ÐµÐ½Ð¸Ñ â€” Ñ‚Ñ‘Ð¼Ð½Ð°Ñ ÐºÐ°Ð¹Ð¼Ð° Ð¸ ÑÐ²ÐµÑ‚Ð»Ñ‹Ð¹ Ð±Ð»Ð¸Ðº,
    // ÑÑ‚Ð¾ Ñ‡Ð¸Ñ‚Ð°ÐµÑ‚ÑÑ Ñ‚Ð°Ðº Ð¶Ðµ, Ð½Ð¾ Ð±ÐµÐ· Ð¿Ð¾Ð¿Ð¸ÐºÑÐµÐ»ÑŒÐ½Ð¾Ð³Ð¾ ÑÐ¼ÐµÑˆÐ¸Ð²Ð°Ð½Ð¸Ñ
    const int ballR = R0 * 10 / 100 < 5 ? 5 : R0 * 10 / 100;
    g->fillCircle(bx, by, ballR + 1, blend565(bg, 0x055F, 0.35f));
    g->fillCircle(bx, by, ballR, 0x02DF);
    g->fillCircle(bx - ballR / 3, by - ballR / 3, ballR / 3 > 1 ? ballR / 3 : 1, 0x6E7F);

    // Ð¡ÑƒÐ¼Ð¼Ð°Ñ€Ð½Ð°Ñ Ð¿ÐµÑ€ÐµÐ³Ñ€ÑƒÐ·ÐºÐ° Ð²Ð½Ð¸Ð·Ñƒ
    char buf[12];
    snprintf(buf, sizeof buf, "%.2f", static_cast<double>(sqrtf(ax * ax + ay * ay)));
    const int numPx = static_cast<int>(r.h * 0.13f) < 10 ? 10 : static_cast<int>(r.h * 0.13f);
    const int numY  = r.y + r.h - static_cast<int>(r.h * 0.07f) - numPx;

    const Text::Face vf = Text::pick(numPx);
    const int vw = Text::width(buf, vf);
    Text::drawBox(g, buf, cx - vw / 2, numY, vw, numPx,
                  Text::HA::Left, VA::Top, vf, fg);
    drawBoxText(g, "G", cx + vw / 2 + numPx / 4, numY, numPx, numPx,
                "left", VA::Top, numPx * 65 / 100, mut, false);
}

// â”€â”€â”€ graph â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintGraph(const Frame& f, JsonObjectConst w, const R& r) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    JsonArrayConst sigs = p["signals"];
    const int ns = static_cast<int>(sigs.size());
    if (ns <= 0) return;

    const char* id = w["id"] | "graph";
    GraphState* st = graphState(id);
    if (!st) return;

    const float mn = pF(p, "min", 0.0f);
    const float mx = pF(p, "max", 100.0f);
    if (mx - mn < 1.0e-6f) return;

    // ÐžÐ´Ð¸Ð½ ÑÐµÐ¼Ð¿Ð» Ð½Ð° Ð¿Ð¸ÐºÑÐµÐ»ÑŒ Ð¿Ð¾ ÑˆÐ¸Ñ€Ð¸Ð½Ðµ Ð¾ÐºÐ½Ð°
    const float windowSec = pF(p, "windowSec", 60.0f);
    const uint32_t stepMs = static_cast<uint32_t>(windowSec * 1000.0f / kGraphPoints);
    const uint32_t now = millis();

    if (now - st->lastMs >= stepMs) {
        st->lastMs = now;
        for (int i = 0; i < ns && i < kGraphSignals; ++i) {
            const char* sid = sigs[i]["signal"] | static_cast<const char*>(nullptr);
            st->v[i][st->head] = sid ? Signals::get(sid) : 0.0f;
        }
        st->head = (st->head + 1) % kGraphPoints;
        if (st->filled < kGraphPoints) ++st->filled;
    }

    const uint16_t bg  = f.bg;
    const uint16_t mut = Layout::themeMuted();

    // Ð¡ÐµÑ‚ÐºÐ° Ð¿Ð¾ Ð³Ð¾Ñ€Ð¸Ð·Ð¾Ð½Ñ‚Ð°Ð»Ð¸ Ñ Ð¿Ð¾Ð´Ð¿Ð¸ÑÑÐ¼Ð¸ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ð¹
    JsonObjectConst t = p["ticks"];
    const float major = t["major"] | 0.0f;
    if (major > 0.0f) {
        const bool     labels  = t["labels"] | false;
        const uint16_t gridCol = blend565(bg, 0xFFFF, 0.08f);

        for (float v = mn; v <= mx + 0.001f; v += major) {
            const int y = r.y + r.h - static_cast<int>((v - mn) / (mx - mn) * r.h);
            g->drawFastHLine(r.x, y, r.w, gridCol);

            if (labels) {
                char lab[12];
                snprintf(lab, sizeof lab, "%g", static_cast<double>(v));
                drawBoxText(g, lab, r.x + 3, y - 12, 40, 11, "left", VA::Top, 10, mut, false);
            }
        }
    }

    // Ð Ð°Ð¼ÐºÐ°
    g->drawRect(r.x, r.y, r.w, r.h, mut);

    // lineWidth Ð¸Ð· Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€Ð°: Ñ€Ð¸ÑÑƒÐµÐ¼ Ð½ÐµÑÐºÐ¾Ð»ÑŒÐºÐ¾ ÑÐ¼ÐµÑ‰Ñ‘Ð½Ð½Ñ‹Ñ… Ð¿Ð¾ Ð²ÐµÑ€Ñ‚Ð¸ÐºÐ°Ð»Ð¸ Ð»Ð¸Ð½Ð¸Ð¹,
    // Ð¿Ð¾Ñ‚Ð¾Ð¼Ñƒ Ñ‡Ñ‚Ð¾ Ñƒ Arduino_GFX Ð½ÐµÑ‚ Ñ‚Ð¾Ð»Ñ‰Ð¸Ð½Ñ‹ Ñƒ drawLine
    const int lw = pI(p, "lineWidth", 1) < 1 ? 1 : pI(p, "lineWidth", 1);

    for (int i = 0; i < ns && i < kGraphSignals; ++i) {
        const uint16_t col = rgb565FromHex(
            sigs[i]["color"] | static_cast<const char*>(nullptr), Layout::themeFg());

        int prevX = 0, prevY = 0;
        for (uint16_t k = 0; k < st->filled; ++k) {
            // Ð˜Ð´Ñ‘Ð¼ Ð¾Ñ‚ ÑÐ°Ð¼Ð¾Ð³Ð¾ ÑÑ‚Ð°Ñ€Ð¾Ð³Ð¾ Ðº ÑÐ°Ð¼Ð¾Ð¼Ñƒ ÑÐ²ÐµÐ¶ÐµÐ¼Ñƒ
            const uint16_t idx = (st->head + kGraphPoints - st->filled + k) % kGraphPoints;
            const float nv = clampf((st->v[i][idx] - mn) / (mx - mn), 0.0f, 1.0f);
            const int x = r.x + static_cast<int>(static_cast<float>(k) * r.w / kGraphPoints);
            const int y = r.y + r.h - static_cast<int>(nv * r.h);
            if (k > 0) {
                for (int o = 0; o < lw; ++o) {
                    g->drawLine(prevX, prevY + o, x, y + o, col);
                }
            }
            prevX = x;
            prevY = y;
        }
    }
}

// â”€â”€â”€ clock â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintClock(const Frame& f, JsonObjectConst w, const R& r) {
    JsonObjectConst p = w["props"];

    // RTC PCF8563 ÐµÑ‰Ñ‘ Ð½Ðµ Ð¿Ð¾Ð´ÐºÐ»ÑŽÑ‡Ñ‘Ð½ â€” Ð²Ñ€ÐµÐ¼Ñ ÑÐ¸Ð½Ñ‚ÐµÐ·Ð¸Ñ€ÑƒÐµÐ¼ Ð¸Ð· uptime.
    const uint32_t s = millis() / 1000;
    char buf[16];
    if (pB(p, "showSeconds", false)) {
        snprintf(buf, sizeof buf, "%lu:%02lu:%02lu",
                 static_cast<unsigned long>((s / 3600) % 24),
                 static_cast<unsigned long>((s / 60) % 60),
                 static_cast<unsigned long>(s % 60));
    } else {
        snprintf(buf, sizeof buf, "%lu:%02lu",
                 static_cast<unsigned long>((s / 3600) % 24),
                 static_cast<unsigned long>((s / 60) % 60));
    }

    const int sz = fitPx(buf, static_cast<int>(r.w * 0.94f), static_cast<int>(r.h * 0.8f));
    drawBoxText(f.gfx, buf, r.x, r.y, r.w, r.h, "center", VA::Mid, sz,
                pC(p, "color", Layout::themeFg()));
}

// â”€â”€â”€ Ð—Ð°Ð³Ð»ÑƒÑˆÐºÐ° Ð´Ð»Ñ Ð½ÐµÑ€ÐµÐ°Ð»Ð¸Ð·Ð¾Ð²Ð°Ð½Ð½Ñ‹Ñ… Ñ‚Ð¸Ð¿Ð¾Ð² â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintPlaceholder(const Frame& f, const R& r, const char* type) {
    auto* g = f.gfx;
    const uint16_t mut = Layout::themeMuted();
    g->drawRect(r.x, r.y, r.w, r.h, mut);
    drawBoxText(g, type, r.x, r.y, r.w, r.h, "center", VA::Mid, 13, mut, false);
}

// â”€â”€â”€ Ð”Ð¸ÑÐ¿ÐµÑ‚Ñ‡ÐµÑ€ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void paintWidget(const Frame& f, JsonObjectConst w) {
    JsonObjectConst rc = w["rect"];
    const R r = {
        (rc["x"] | 0) + f.shiftX,
        (rc["y"] | 0) + f.shiftY,
        rc["w"] | 0,
        rc["h"] | 0,
    };
    if (r.w <= 0 || r.h <= 0) return;

    const char* type  = w["type"] | "";
    const char* sigId = w["signal"] | static_cast<const char*>(nullptr);
    const float value = sigId ? Signals::get(sigId) : 0.0f;

    if      (strcmp(type, "arc_gauge")   == 0) paintArcGauge(f, w, r, value);
    else if (strcmp(type, "numeric")     == 0) paintNumeric(f, w, r, value);
    else if (strcmp(type, "label")       == 0) paintLabel(f, w, r);
    else if (strcmp(type, "bar")         == 0) paintBar(f, w, r, value);
    else if (strcmp(type, "shift_light") == 0) paintShiftLight(f, w, r, value);
    else if (strcmp(type, "warning")     == 0) paintWarning(f, w, r);
    else if (strcmp(type, "gforce")      == 0) paintGForce(f, w, r);
    else if (strcmp(type, "graph")       == 0) paintGraph(f, w, r);
    else if (strcmp(type, "clock")       == 0) paintClock(f, w, r);
    else if (strcmp(type, "steering")    == 0) {
        // ÐžÑ‚ÑÑƒÑ‚ÑÑ‚Ð²ÑƒÑŽÑ‰Ð¸Ð¹ ÑÐ¸Ð³Ð½Ð°Ð» = Ñ€ÑƒÐ»ÑŒ Ð¿Ð¾ Ñ†ÐµÐ½Ñ‚Ñ€Ñƒ. ÐžÐ±Ñ‰Ð¸Ð¹ fallback Ð² 0 Ð·Ð´ÐµÑÑŒ Ð½Ðµ
        // Ð¿Ð¾Ð´Ñ…Ð¾Ð´Ð¸Ñ‚: 0 â€” ÑÑ‚Ð¾ Ð·Ð°ÐºÐ¾Ð½Ð½Ð¾Ðµ Â«Ð¿Ð¾Ð»Ð½Ð¾ÑÑ‚ÑŒÑŽ Ð²Ð»ÐµÐ²Ð¾Â».
        paintSteering(f, w, r, sigId && Signals::has(sigId) ? value : 0.5f);
    }
    else paintPlaceholder(f, r, type);
}

} // namespace

// â”€â”€â”€ ÐŸÑƒÐ±Ð»Ð¸Ñ‡Ð½Ñ‹Ð¹ Ð²Ñ…Ð¾Ð´ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void screen(const Frame& f, uint8_t screenIdx) {
    JsonArrayConst ws = Layout::widgets(screenIdx);

    // Ð¤Ð¾Ð½ Ð·Ð´ÐµÑÑŒ ÐÐ• Ð·Ð°Ð»Ð¸Ð²Ð°ÐµÑ‚ÑÑ: Ð²Ð¾ Ð²Ñ€ÐµÐ¼Ñ ÑÐ²Ð°Ð¹Ð¿Ð° Ð² Ð¾Ð´Ð¸Ð½ ÐºÐ°Ð´Ñ€ Ñ€Ð¸ÑÑƒÑŽÑ‚ÑÑ Ð´Ð²Ð°
    // ÑÐºÑ€Ð°Ð½Ð° ÑÐ¾ ÑÐ´Ð²Ð¸Ð³Ð¾Ð¼, Ð¸ Ð·Ð°Ð»Ð¸Ð²ÐºÐ° Ð²Ð½ÑƒÑ‚Ñ€Ð¸ ÑÑ‚Ð¾Ð¹ Ñ„ÑƒÐ½ÐºÑ†Ð¸Ð¸ ÑÑ‚Ñ‘Ñ€Ð»Ð° Ð±Ñ‹ Ð¿ÐµÑ€Ð²Ñ‹Ð¹.
    // ÐžÑ‡Ð¸ÑÑ‚ÐºÑƒ Ð´ÐµÐ»Ð°ÐµÑ‚ Ð²Ñ‹Ð·Ñ‹Ð²Ð°ÑŽÑ‰Ð¸Ð¹ â€” ÑÐ¼. loop() Ð² main.cpp.

    // ÐŸÐ¾Ñ€ÑÐ´Ð¾Ðº Ð¿Ð¾ z: ÑÐ¾Ñ€Ñ‚Ð¸Ñ€ÑƒÐµÐ¼ Ð¸Ð½Ð´ÐµÐºÑÑ‹, ÑÐ°Ð¼ JSON Ð½Ðµ Ñ‚Ñ€Ð¾Ð³Ð°ÐµÐ¼.
    // Ð’Ð¸Ð´Ð¶ÐµÑ‚Ð¾Ð² ÐµÐ´Ð¸Ð½Ð¸Ñ†Ñ‹, Ð¿Ð¾ÑÑ‚Ð¾Ð¼Ñƒ Ð²ÑÑ‚Ð°Ð²ÐºÐ°Ð¼Ð¸ â€” Ð´ÐµÑˆÐµÐ²Ð»Ðµ Ð»ÑŽÐ±Ð¾Ð¹ Ð°Ð»ÑŒÑ‚ÐµÑ€Ð½Ð°Ñ‚Ð¸Ð²Ñ‹.
    const int n = static_cast<int>(ws.size());
    constexpr int kMaxWidgets = 32;
    uint8_t order[kMaxWidgets];
    const int cnt = n < kMaxWidgets ? n : kMaxWidgets;

    for (int i = 0; i < cnt; ++i) order[i] = static_cast<uint8_t>(i);
    for (int i = 1; i < cnt; ++i) {
        const uint8_t cur = order[i];
        const int zc = ws[cur]["z"] | 0;
        int j = i - 1;
        while (j >= 0 && (ws[order[j]]["z"] | 0) > zc) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = cur;
    }

    for (int i = 0; i < cnt; ++i) {
        paintWidget(f, ws[order[i]].as<JsonObjectConst>());
    }
}

} // namespace Render
