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

// Выравнивание берём из модуля текста, чтобы не держать два одинаковых enum
using VA = Text::VA;

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * Какую часть виджета рисовать.
 *
 * Разделение на слои работает с точностью до виджета, и для крупных виджетов
 * этого мало. У радара G-force диск, кольца и подписи от сигналов не зависят,
 * но перерисовывались каждый кадр вместе с шариком — одна заливка диска это
 * 106 тысяч пикселей. То же у графика с сеткой.
 *
 * Такие виджеты рисуются в двух проходах: неподвижная оправа уходит в
 * статический слой, живая часть рисуется поверх неё каждый кадр.
 */
enum class Part : uint8_t {
    All,     ///< всё сразу — свайп и виджеты без разделения
    Chrome,  ///< только неподвижная оправа
    Live,    ///< только то, что меняется от кадра к кадру
};

/// Объединяющий прямоугольник. Пустые (w или h <= 0) игнорируются.
R unite(const R& a, const R& b) {
    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    const int x0 = a.x < b.x ? a.x : b.x;
    const int y0 = a.y < b.y ? a.y : b.y;
    const int x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const int y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return R{x0, y0, x1 - x0, y1 - y0};
}

/// Пересечение прямоугольника с рамкой. Возвращает пустой, если не пересекся.
R clipTo(const R& a, const R& bounds) {
    const int x0 = a.x > bounds.x ? a.x : bounds.x;
    const int y0 = a.y > bounds.y ? a.y : bounds.y;
    const int x1 = (a.x + a.w) < (bounds.x + bounds.w) ? (a.x + a.w) : (bounds.x + bounds.w);
    const int y1 = (a.y + a.h) < (bounds.y + bounds.h) ? (a.y + a.h) : (bounds.y + bounds.h);
    return (x1 > x0 && y1 > y0) ? R{x0, y0, x1 - x0, y1 - y0} : R{0, 0, 0, 0};
}

// ─── Доступ к пропам ─────────────────────────────────────────────────────────

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

/// Цвет зоны, в которую попадает значение.
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

// ─── Текст ───────────────────────────────────────────────────────────────────
//
// Размер везде задаётся высотой цифр в пикселях — той же величиной, что и
// fontSize в редакторе. Модуль Text прижимает её к ближайшей доступной
// ступени, потому что глифы это битмапы и масштабируются только целым.

/// Высота цифр, при которой строка вписывается в бокс.
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
                  Text::pick(s, px, bold), color);
}

void drawCenteredText(Arduino_GFX* g, const char* s, int cx, int cy,
                      int px, uint16_t color, bool bold = true) {
    if (!s || !*s) return;
    g->setTextWrap(false);
    Text::drawCentered(g, s, cx, cy, Text::pick(s, px, bold), color);
}

/**
 * Тронут ли кадр за пределами рамок виджетов.
 *
 * Сбрасывается перед каждым проходом отрисовки и поднимается заливками во всю
 * площадь. Раньше факт оверлея угадывался по типу виджета: если на экране есть
 * Warning, кадр считался испорченным всегда. Из-за этого на таком экране
 * каждый кадр шли restoreAll() и полная отправка, и двухслойная композиция не
 * давала ничего — а Warning стоит почти на каждом экране.
 */
bool s_overlayDrawn = false;

/// Быстрая заливка экрана готовым цветом без чтения фона.
///
/// Цвет уже смешан заранее (blend565 один раз на кадр). Записывает pre-swapped
/// пиксели по 32 бита — вдвое меньше обращений к PSRAM, ~2 мс на полный экран.
/// tintScreen удалён: он читал фон из PSRAM (медленно) и не учитывал pre-swap.
void fillScreen(uint16_t color) {
    uint16_t* fb = Display::framebuffer();
    if (!fb) return;
    s_overlayDrawn = true;
    const size_t n = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT;
    // color уже в native RGB565 от blend565 — swap16 перед записью.
    const uint16_t s = swap16(color);
    const uint32_t pair = (static_cast<uint32_t>(s) << 16) | s;
    uint32_t* fb32 = reinterpret_cast<uint32_t*>(fb);
    const size_t n32 = n / 2;
    for (size_t i = 0; i < n32; ++i) fb32[i] = pair;
}



// ─── Состояние виджетов между кадрами ────────────────────────────────────────

constexpr uint8_t  kWarnPool     = 6;
constexpr uint8_t  kGraphPool    = 3;
constexpr uint8_t  kGraphSignals = 4;
constexpr uint16_t kGraphPoints  = 120;
constexpr uint8_t  kIdLen        = 24;
constexpr uint8_t  kBallPool     = 2;

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

/// След шарика G-force: кольцевой буфер последних позиций.
/// Положение шарика G-force между кадрами.
///
/// Хранится ровно одна точка. Раньше здесь был кольцевой буфер на 30 позиций
/// под след, и по нему же считалась живая область виджета — из-за чего она
/// разрасталась почти на весь радар: за 30 кадров при 11 FPS шарик успевает
/// обойти его целиком. Со снятым следом область сжимается до самого шарика.
struct BallState {
    char    id[kIdLen];
    bool    used;
    bool    seen;   ///< позиция уже записана хотя бы раз
    int16_t x, y;
    /// Область, занятая шариком и цифрами на предыдущем кадре.
    ///
    /// Нужна, чтобы восстанавливать и отправлять только её вместо всей рамки
    /// радара. Прошлый кадр надо затереть, текущий — отправить, поэтому
    /// liveRect() возвращает объединение прошлой области с текущей.
    R       live;
};
BallState s_ball[kBallPool] = {};

BallState* ballState(const char* id) {
    for (auto& b : s_ball) if (b.used && strncmp(b.id, id, kIdLen) == 0) return &b;
    for (auto& b : s_ball) {
        if (!b.used) {
            b.used = true;
            strncpy(b.id, id, kIdLen - 1);
            b.id[kIdLen - 1] = '\0';
            b.seen = false;
            b.live = R{0, 0, 0, 0};
            return &b;
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

// ─── numeric ─────────────────────────────────────────────────────────────────

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

    // Размер значения: явный fontSize либо автоподгонка — как в редакторе
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

    // Подписи пропорциональны значению, чтобы виджет рос целиком
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

// ─── label ───────────────────────────────────────────────────────────────────

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

// ─── bar ─────────────────────────────────────────────────────────────────────

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

// ─── steering ────────────────────────────────────────────────────────────────

void paintSteering(const Frame& f, JsonObjectConst w, const R& r, float pos) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    // -1 влево … 0 центр … +1 вправо
    float n = clampf((pos - 0.5f) * 2.0f, -1.0f, 1.0f);

    // Зона нечувствительности: остаток диапазона растягивается обратно на 0..1,
    // иначе полоса теряла бы часть хода и не доходила до края
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

    // Риска центра поверх заполнения — ноль виден при любом отклонении
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

// ─── tire_temp ───────────────────────────────────────────────────────────────

/**
 * Шкала температуры: синий → красный через голубой, зелёный, жёлтый, оранжевый.
 *
 * Зелёный посажен в середину диапазона намеренно: это рабочая температура, и
 * «всё зелёное» должно читаться как «всё в порядке». Синее — резина не
 * прогрелась, красное — перегрев.
 *
 * Стопы совпадают с SCALE в paintTireTemp.ts — менять только парой.
 */
uint16_t tireColor(float n01) {
    struct Stop { float p; uint8_t r, g, b; };
    static constexpr Stop S[] = {
        { 0.00f,  10,  60, 200 },   // синий
        { 0.22f,   0, 190, 235 },   // голубой
        { 0.44f,  20, 205,  90 },   // зелёный
        { 0.64f, 235, 210,  20 },   // жёлтый
        { 0.82f, 250, 140,  15 },   // оранжевый
        { 1.00f, 240,  45,  40 },   // красный
    };
    constexpr int N = sizeof(S) / sizeof(S[0]);

    const float p = clampf(n01, 0.0f, 1.0f);

    int i = 0;
    while (i < N - 2 && p > S[i + 1].p) ++i;

    const float t = (p - S[i].p) / (S[i + 1].p - S[i].p);
    const int r = S[i].r + static_cast<int>(lroundf((S[i + 1].r - S[i].r) * t));
    const int g = S[i].g + static_cast<int>(lroundf((S[i + 1].g - S[i].g) * t));
    const int b = S[i].b + static_cast<int>(lroundf((S[i + 1].b - S[i].b) * t));

    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/// Горизонтальный отступ контура скруглённого угла. dy отсчитывается от края
/// шины внутрь: 0 — самая крайняя строка, где вырез максимален.
int roundInset(int dy, int rad) {
    if (rad <= 0 || dy >= rad) return 0;
    const float k = static_cast<float>(rad - dy);
    return rad - static_cast<int>(lroundf(sqrtf(static_cast<float>(rad) * rad - k * k)));
}

/**
 * Сектор шины.
 *
 * В Arduino_GFX нет отсечения по контуру, поэтому у наружных секторов крайние
 * строки поджимаются под скругление вручную. Середина сектора при этом уходит
 * одним вызовом: скругление задевает только первые и последние rad строк, а их
 * единицы — заливать построчно всю высоту было бы вдесятеро дороже без всякой
 * разницы в картинке.
 */
void fillSector(Arduino_GFX* g, int x, int y, int w, int h,
                int rad, bool roundLeft, bool roundRight, uint16_t col) {
    if (w <= 0 || h <= 0) return;

    // Средние секторы скругление не касается — прямоугольник целиком
    if (rad <= 0 || (!roundLeft && !roundRight)) {
        g->fillRect(x, y, w, h, col);
        return;
    }

    const int cap = rad < h / 2 ? rad : h / 2;

    if (h - cap * 2 > 0) g->fillRect(x, y + cap, w, h - cap * 2, col);

    for (int dy = 0; dy < cap; ++dy) {
        const int ins = roundInset(dy, rad);
        const int x0  = roundLeft  ? x + ins     : x;
        const int x1  = roundRight ? x + w - ins : x + w;
        if (x1 <= x0) continue;
        g->fillRect(x0, y + dy,         x1 - x0, 1, col);
        g->fillRect(x0, y + h - 1 - dy, x1 - x0, 1, col);
    }
}

/**
 * Температура шин по секторам.
 *
 * Четыре шины стоят по местам колёс, как вид на машину сверху. Каждая — это
 * вертикальный прямоугольник, поделённый на 4 вертикальных сектора по ширине
 * протектора, и каждый сектор красится своей температурой.
 *
 * Правый борт зеркалится: сектор с ВНУТРЕННИМ плечом всегда смотрит к центру
 * виджета. Иначе пришлось бы держать в голове, что у левой шины внутреннее
 * плечо справа, а у правой слева, и картинка перестала бы читаться с одного
 * взгляда — а в ней весь смысл.
 *
 * Соотношение плеч — то, ради чего это и смотрят: если внутренние секторы
 * горячее наружных даже в поворотах, развала слишком много.
 *
 * Раскладка повторяет paintTireTemp.ts.
 */
void paintTireTemp(const Frame& f, JsonObjectConst w, const R& r) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const char* prefix = pS(p, "prefix", "tire");
    const float mn   = pF(p, "min", 40.0f);
    const float mx   = pF(p, "max", 110.0f);
    const float span = (mx - mn) != 0.0f ? (mx - mn) : 1.0f;

    const bool showValue = pB(p, "showValue", true);
    const bool showLabel = pB(p, "showLabel", false);
    const int  gapX   = pI(p, "gapX", 18);
    const int  gapY   = pI(p, "gapY", 14);
    const int  secGap = pI(p, "sectorGap", 1);

    const int tileW = (r.w - gapX) / 2;
    const int tileH = (r.h - gapY) / 2;
    if (tileW < 6 || tileH < 10) return;

    const int t26 = tileH * 26 / 100;
    const int t20 = tileH * 20 / 100;
    const int textH  = showValue ? (t26 < 15 ? t26 : 15) : 0;
    const int labelH = showLabel ? (t20 < 11 ? t20 : 11) : 0;

    int tireH = tileH - textH - labelH;
    if (tireH < 6) tireH = 6;

    int rad = pI(p, "radius", 4);
    if (rad > tileW / 2) rad = tileW / 2;
    if (rad > tireH / 2) rad = tireH / 2;

    const uint16_t track = pC(p, "trackColor", 0x18E3);
    const int      secW  = (tileW - secGap * 3) / 4;
    if (secW <= 0) return;

    /// mirror — правый борт: секторы выкладываются справа налево.
    struct Spec { const char* key; const char* label; uint8_t col, row; bool mirror; };
    static constexpr Spec SPECS[4] = {
        { "fl", "FL", 0, 0, false },
        { "fr", "FR", 1, 0, true  },
        { "rl", "RL", 0, 1, false },
        { "rr", "RR", 1, 1, true  },
    };

    char id[Signals::kMaxIdLen];
    char buf[12];

    for (const Spec& s : SPECS) {
        const int ox = r.x + s.col * (tileW + gapX);
        const int oy = r.y + s.row * (tileH + gapY);

        if (showLabel) {
            drawBoxText(g, s.label, ox, oy, tileW, labelH, "center", VA::Top,
                        labelH * 85 / 100, Layout::themeMuted(), false);
        }

        const int ty = oy + labelH;

        // Дорожка под секторами: она же проступает в зазорах между ними
        g->fillRoundRect(ox, ty, tileW, tireH, rad, track);

        float sum  = 0.0f;
        int   seen = 0;

        for (int i = 0; i < 4; ++i) {
            // Зеркалим правый борт: внутреннее плечо смотрит к центру виджета
            const int idx = s.mirror ? 3 - i : i;
            snprintf(id, sizeof id, "%s.%s.t%d", prefix, s.key, idx + 1);

            // Нет сигнала — остаётся дорожка, чтобы отсутствие данных было видно
            if (!Signals::has(id)) continue;

            const float v = Signals::get(id);
            sum += v;
            ++seen;

            fillSector(g, ox + i * (secW + secGap), ty, secW, tireH, rad,
                       i == 0, i == 3, tireColor((v - mn) / span));
        }

        if (showValue) {
            // Среднее по тем секторам, что реально пришли: при частично
            // подключённых датчиках цифра остаётся осмысленной, а не падает
            // вдвое от делённых на четыре двух значений.
            if (seen > 0) {
                snprintf(buf, sizeof buf, "%d\u00B0", static_cast<int>(lroundf(sum / seen)));
            } else {
                snprintf(buf, sizeof buf, "-");
            }
            drawBoxText(g, buf, ox, ty + tireH, tileW, textH, "center", VA::Bottom,
                        textH * 82 / 100,
                        seen > 0 ? Layout::themeFg() : Layout::themeMuted(), false);
        }
    }
}

// ─── shift_light ─────────────────────────────────────────────────────────────

/// Дуга режима arc. Значения совпадают с paintShiftLight.ts — менять только
/// вместе с редактором, иначе точки снова разъедутся.
constexpr float kArcPad   = 2.0f;
constexpr float kArcStart = -170.0f * PI / 180.0f;
constexpr float kArcEnd   =  -10.0f * PI / 180.0f;
/// Цвет неактивной точки (#2C2C2E из редактора), кладётся с alpha 0.10
constexpr uint16_t kArcTrack = 0x2965;

/// Длина эллиптической дуги, численно. Тот же метод и то же число шагов, что
/// в редакторе: количество точек считается из этой длины, и другая формула
/// дала бы другое количество.
float ellipseArcLen(float a, float b, float t1, float t2) {
    // Полуоси зависят только от рамки, а она за кадр не меняется. Считаем
    // один раз: иначе это 128 вызовов cos/sin на каждый кадр.
    static float sA = -1.0f, sB = -1.0f, sLen = 0.0f;
    if (a == sA && b == sB) return sLen;

    constexpr int kSteps = 64;
    float len = 0.0f;
    for (int i = 0; i < kSteps; ++i) {
        const float ta = t1 + (t2 - t1) * i / kSteps;
        const float tb = t1 + (t2 - t1) * (i + 1) / kSteps;
        const float dx = a * (cosf(tb) - cosf(ta));
        const float dy = b * (sinf(tb) - sinf(ta));
        len += sqrtf(dx * dx + dy * dy);
    }
    sA = a; sB = b; sLen = len;
    return len;
}

void paintShiftLight(const Frame& f, JsonObjectConst w, const R& r, float rpm) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    JsonArrayConst stages = p["stages"];
    const int n = static_cast<int>(stages.size());
    if (n <= 0) return;

    const char* mode = pS(p, "mode", "segments");

    // Мигание последней ступени на отсечке
    auto blinkOn = [&](JsonObjectConst st) -> bool {
        const float hz = st["blinkHz"] | 0.0f;
        if (hz <= 0.0f) return true;
        return fmodf(f.timeSec * hz, 1.0f) < 0.5f;
    };

    if (strcmp(mode, "flash") == 0) {
        // Полноэкранная вспышка при достижении последней ступени.
        // fillScreen вместо tintScreen: цвет смешивается один раз с фоном экрана,
        // чтение 217k пикселей PSRAM исключается, трафик шины падает вдвое (~2 мс
        // вместо ~12 мс). Визуально неотличимо — на тёмном фоне blend565(bg, c,
        // 0.45f) даёт тот же насыщенный оттенок, что и попиксельный tintScreen.
        JsonObjectConst last = stages[n - 1];
        if (rpm >= (last["at"] | 0.0f) && blinkOn(last)) {
            const uint16_t fc = rgb565FromHex(last["color"] | static_cast<const char*>(nullptr), 0xF800);
            fillScreen(blend565(f.bg, fc, 0.45f));
        }
        return;
    }

    if (strcmp(mode, "arc") == 0) {
        // Геометрия и количество точек — по формулам paintShiftLight.ts.
        // Раньше здесь рисовалось ровно stages.size() точек по полуокружности
        // от 180 до 360 градусов, из-за чего на устройстве точек было в разы
        // меньше и лежали они по другой кривой, чем в редакторе.
        const float ea  = (r.w / 2.0f - kArcPad) * 0.97f;   // полуось X
        const float eb  = (r.h > 8 ? r.h : 8) * 0.92f;      // полуось Y
        const float ecx = r.x + r.w / 2.0f;
        const float ecy = r.y + static_cast<float>(r.h);    // центр у нижнего края

        const int   dotSize = pI(p, "dotSize", 0);
        // dotSize из редактора — это радиус, а не диаметр
        const float autoR = clampf(r.h * 0.40f, 3.0f, 12.0f);
        const float dotR  = dotSize > 0 ? static_cast<float>(dotSize) : autoR;

        const float arcLen = ellipseArcLen(ea, eb, kArcStart, kArcEnd);
        int dotCount = static_cast<int>(arcLen / (dotR * 2.0f + dotR * 0.6f));
        if (dotCount > 30) dotCount = 30;
        if (dotCount < n)  dotCount = n;

        // Заполнение линейное по диапазону ступеней, а не «точка на ступень»
        const float vMin = stages[0]["at"] | 0.0f;
        const float vMax = stages[n - 1]["at"] | 0.0f;
        const float frac = clampf((rpm - vMin) / (vMax - vMin + 1.0f), 0.0f, 1.0f);
        const int   litCount = static_cast<int>(lroundf(frac * dotCount));

        // На отсечке мигают все точки разом, а не последняя ступень
        const bool allLit  = rpm >= vMax;
        const bool flashOn = blinkOn(stages[n - 1]);

        const int ir = static_cast<int>(lroundf(dotR)) > 1
                     ? static_cast<int>(lroundf(dotR)) : 1;

        for (int i = 0; i < dotCount; ++i) {
            const float t = dotCount > 1 ? static_cast<float>(i) / (dotCount - 1) : 0.0f;
            const float ang = kArcStart + t * (kArcEnd - kArcStart);
            const int dx = static_cast<int>(lroundf(ecx + ea * cosf(ang)));
            const int dy = static_cast<int>(lroundf(ecy + eb * sinf(ang)));

            const int stageIdx = (i * n / dotCount) < n - 1 ? (i * n / dotCount) : n - 1;
            const uint16_t col = rgb565FromHex(
                stages[stageIdx]["color"] | static_cast<const char*>(nullptr), 0xFFFF);

            uint16_t fill;
            if (allLit)          fill = flashOn ? col : blend565(f.bg, col, 0.05f);
            else if (i < litCount) fill = col;
            else                 fill = blend565(f.bg, kArcTrack, 0.10f);

            g->fillCircle(dx, dy, ir, fill);
        }
        return;
    }

    // segments (по умолчанию)
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

// ─── warning ─────────────────────────────────────────────────────────────────

void paintWarning(const Frame& f, JsonObjectConst w, const R& /*r*/) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const char* id = w["id"] | "warn";
    WarnState* st = warnState(id);
    if (!st) return;

    // Условие срабатывания: пороги из редактора либо when{} из схемы
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
    if (triggered) st->showUntilMs = now + 2000;   // автопропадание через 2 с
    if (now >= st->showUntilMs) return;

    // Аварийное сообщение — оверлей всего экрана, а не содержимое рамки.
    //
    // rect умышленно игнорируется: предупреждение о падении давления масла
    // должно читаться мгновенно и не может зависеть от того, в какой угол его
    // положили в редакторе. Редактор рисует его так же.
    //
    // Плоская заливка одним проходом. Было три: затемнение всего кадра,
    // радиальный градиент из 130 концентрических окружностей и векторный
    // треугольник. На кадрах со сработавшей аварией это давало провал до
    // 8 FPS при 90-110 мс рендера — два полноэкранных проходa по PSRAM вместо
    // одного.
    //
    // fillScreen вместо tintScreen: цвет смешивается один раз с фоном экрана
    // вместо 217k раз по пикселям. Только записи PSRAM, чтений нет — ~2 мс
    // вместо ~12 мс. На чёрном фоне blend565(bg, red, 0.70f) визуально
    // идентично попиксельному tintScreen.
    const uint16_t col = pC(p, "color", Layout::themeCrit());
    fillScreen(blend565(f.bg, col, 0.70f));

    const int cx = LCD_WIDTH / 2;
    const int cy = LCD_HEIGHT / 2;

    const char* label    = pS(p, "label", "WARNING");
    const bool  hasLabel = label && *label;

    const char* unit = w["unit"] | "";
    char buf[24];
    snprintf(buf, sizeof buf, "%.*f%s%s",
             strcmp(unit, "bar") == 0 ? 2 : (shown > 100.0f ? 0 : 1),
             static_cast<double>(shown), *unit ? " " : "", unit);

    // Подпись и значение по центру экрана. Кегли — доли ширины панели, чтобы
    // текст читался с водительского места без настройки.
    const int labelPx = LCD_WIDTH * 13 / 100;
    const int valuePx = LCD_WIDTH * 10 / 100;

    if (hasLabel) {
        drawBoxText(g, label, 0, cy - labelPx, LCD_WIDTH, labelPx,
                    "center", VA::Top, labelPx, 0xFFFF);
        drawBoxText(g, buf, 0, cy + labelPx / 5, LCD_WIDTH, valuePx,
                    "center", VA::Top, valuePx, 0xFFFF, false);
    } else {
        drawBoxText(g, buf, 0, cy - valuePx / 2, LCD_WIDTH, valuePx,
                    "center", VA::Top, valuePx, 0xFFFF, false);
    }
}

// ─── gforce ──────────────────────────────────────────────────────────────────

/**
 * Геометрия радара.
 *
 * Вынесена отдельно, потому что ею пользуются и отрисовка, и расчёт живой
 * области. Разойдись эти два места хотя бы на пиксель — на экране остался бы
 * след от шарика, и заметно это было бы только в движении.
 */
struct GForceGeom {
    bool ok;
    int  cx, cy, R0;
    int  ballR;
    int  numY, numPx;   ///< полоса с цифрами внизу
};

GForceGeom gforceGeom(const R& r) {
    GForceGeom gm{};

    // Радар занимает верхние ~80% высоты, снизу остаётся полоса под цифры —
    // ровно та же раскладка, что в редакторе
    const int radarSize = r.w < static_cast<int>(r.h * 0.80f)
                            ? r.w : static_cast<int>(r.h * 0.80f);
    gm.R0 = radarSize / 2;
    gm.ok = gm.R0 >= 8;

    gm.cx    = r.x + r.w / 2;
    gm.cy    = r.y + gm.R0 + static_cast<int>(r.h * 0.04f);
    gm.ballR = gm.R0 * 10 / 100 < 5 ? 5 : gm.R0 * 10 / 100;
    gm.numPx = static_cast<int>(r.h * 0.13f) < 10 ? 10 : static_cast<int>(r.h * 0.13f);
    gm.numY  = r.y + r.h - static_cast<int>(r.h * 0.07f) - gm.numPx;

    return gm;
}

void paintGForce(const Frame& f, JsonObjectConst w, const R& r, Part part) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    const float range = pF(p, "range", 2.0f);
    const int   rings = pI(p, "rings", 2);
    if (range <= 0.0f || rings < 1) return;

    const GForceGeom gm = gforceGeom(r);
    if (!gm.ok) return;
    const int cx = gm.cx, cy = gm.cy, R0 = gm.R0;

    const uint16_t bg  = f.bg;
    const uint16_t fg  = Layout::themeFg();
    const uint16_t mut = Layout::themeMuted();

    // ── Оправа: диск, кольца, крестовина, подписи осей ──────────────────────
    //
    // От сигналов не зависит ничего, поэтому при разделении на слои всё это
    // рисуется один раз при входе на экран. Раньше перерисовывалось каждый
    // кадр, и одна только заливка диска — это 106 тысяч пикселей.
    if (part != Part::Live) {
        g->fillCircle(cx, cy, R0, blend565(bg, 0xFFFF, 0.04f));
        g->drawCircle(cx, cy, R0, blend565(bg, 0xFFFF, 0.15f));

        // Кольца с подписями в G
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

        // Крестовина пунктиром: в Arduino_GFX нет setLineDash, поэтому штрихи
        // выкладываются вручную с тем же шагом 3/5, что в редакторе
        const uint16_t crossCol = blend565(bg, 0xFFFF, 0.12f);
        for (int d = -R0; d <= R0; d += 8) {
            g->drawFastHLine(cx + d, cy, 3, crossCol);
            g->drawFastVLine(cx, cy + d, 3, crossCol);
        }

        const int axisPx = R0 * 10 / 100 < 9 ? 9 : R0 * 10 / 100;
        const uint16_t axisCol = blend565(bg, 0xFFFF, 0.20f);
        drawCenteredText(g, "BRAKE", cx, cy - R0 + axisPx, axisPx, axisCol, false);
        drawCenteredText(g, "ACCEL", cx, cy + R0 - axisPx, axisPx, axisCol, false);
    }

    if (part == Part::Chrome) return;

    // ── Живая часть: шарик со следом и цифры ────────────────────────────────

    const float ax = Signals::get(p["signalX"] | "imu.ax");
    const float ay = Signals::get(p["signalY"] | "imu.ay");

    // Инерция, а не ускорение: акселерометр даёт ускорение кузова, а водитель
    // ощущает силу в противоположную сторону. Разгон уводит шарик вниз,
    // поворот вправо — влево. Знаки инвертированы, как в редакторе.
    const int bx = cx - static_cast<int>(clampf(ax / range, -1.0f, 1.0f) * R0);
    const int by = cy + static_cast<int>(clampf(ay / range, -1.0f, 1.0f) * R0);

    // Позиция запоминается для расчёта живой области виджета
    if (BallState* bs = ballState(w["id"] | "gforce")) {
        bs->x = static_cast<int16_t>(bx);
        bs->y = static_cast<int16_t>(by);
        bs->seen = true;
    }

    // Шарик: вместо градиента и свечения — тёмная кайма и светлый блик,
    // это читается так же, но без попиксельного смешивания
    const int ballR = gm.ballR;
    g->fillCircle(bx, by, ballR + 1, blend565(bg, 0x055F, 0.35f));
    g->fillCircle(bx, by, ballR, 0x02DF);
    g->fillCircle(bx - ballR / 3, by - ballR / 3, ballR / 3 > 1 ? ballR / 3 : 1, 0x6E7F);

    // Суммарная перегрузка внизу
    char buf[12];
    snprintf(buf, sizeof buf, "%.2f", static_cast<double>(sqrtf(ax * ax + ay * ay)));
    const int numPx = gm.numPx;
    const int numY  = gm.numY;

    const Text::Face vf = Text::pick(buf, numPx);
    const int vw = Text::width(buf, vf);
    Text::drawBox(g, buf, cx - vw / 2, numY, vw, numPx,
                  Text::HA::Left, VA::Top, vf, fg);
    drawBoxText(g, "G", cx + vw / 2 + numPx / 4, numY, numPx, numPx,
                "left", VA::Top, numPx * 65 / 100, mut, false);
}

// ─── graph ───────────────────────────────────────────────────────────────────

void paintGraph(const Frame& f, JsonObjectConst w, const R& r, Part part) {
    JsonObjectConst p = w["props"];
    auto* g = f.gfx;

    JsonArrayConst sigs = p["signals"];
    const int ns = static_cast<int>(sigs.size());
    if (ns <= 0) return;

    const float mn = pF(p, "min", 0.0f);
    const float mx = pF(p, "max", 100.0f);
    if (mx - mn < 1.0e-6f) return;

    const uint16_t bg  = f.bg;
    const uint16_t mut = Layout::themeMuted();

    // ── Оправа: сетка с подписями и рамка ───────────────────────────────────
    // Считается по min/max из пропов, то есть от сигналов не зависит.
    if (part != Part::Live) {
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

        g->drawRect(r.x, r.y, r.w, r.h, mut);
    }

    if (part == Part::Chrome) return;

    // ── Живая часть: набор истории и сами кривые ────────────────────────────

    const char* id = w["id"] | "graph";
    GraphState* st = graphState(id);
    if (!st) return;

    // Один семпл на пиксель по ширине окна
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

    // lineWidth из редактора: рисуем несколько смещённых по вертикали линий,
    // потому что у Arduino_GFX нет толщины у drawLine
    const int lw = pI(p, "lineWidth", 1) < 1 ? 1 : pI(p, "lineWidth", 1);

    for (int i = 0; i < ns && i < kGraphSignals; ++i) {
        const uint16_t col = rgb565FromHex(
            sigs[i]["color"] | static_cast<const char*>(nullptr), Layout::themeFg());

        int prevX = 0, prevY = 0;
        for (uint16_t k = 0; k < st->filled; ++k) {
            // Идём от самого старого к самому свежему
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

// ─── clock ───────────────────────────────────────────────────────────────────

void paintClock(const Frame& f, JsonObjectConst w, const R& r) {
    JsonObjectConst p = w["props"];

    // RTC PCF8563 ещё не подключён — время синтезируем из uptime.
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

// ─── Заглушка для нереализованных типов ──────────────────────────────────────

void paintPlaceholder(const Frame& f, const R& r, const char* type) {
    auto* g = f.gfx;
    const uint16_t mut = Layout::themeMuted();
    g->drawRect(r.x, r.y, r.w, r.h, mut);
    drawBoxText(g, type, r.x, r.y, r.w, r.h, "center", VA::Mid, 13, mut, false);
}

// ─── Диспетчер ───────────────────────────────────────────────────────────────

/// Меняется ли виджет от кадра к кадру.
///
/// Подписи и картинки не зависят ни от сигналов, ни от времени, поэтому их
/// место — в статическом слое. Часы формально меняются, но раз в секунду;
/// держать их в динамике проще, чем изобретать третий класс.
bool isStatic(const char* type) {
    return strcmp(type, "label") == 0 || strcmp(type, "image") == 0;
}

/// Рисует ли виджет за пределами своей рамки.
///
/// Авария и вспышка шифт-лайта затемняют или заливают весь экран. Для
/// восстановления фона это значит, что прямоугольником виджета не отделаться.
bool isFullScreen(JsonObjectConst w, const char* type) {
    if (strcmp(type, "warning") == 0) return true;
    if (strcmp(type, "shift_light") == 0) {
        const char* mode = w["props"]["mode"] | "segments";
        return strcmp(mode, "flash") == 0;
    }
    return false;
}

/// Рамка виджета с учётом сдвига сцены.
R rectOf(const Frame& f, JsonObjectConst w) {
    JsonObjectConst rc = w["rect"];
    return R{
        (rc["x"] | 0) + f.shiftX,
        (rc["y"] | 0) + f.shiftY,
        rc["w"] | 0,
        rc["h"] | 0,
    };
}

/// Виджеты с неподвижной оправой, которую можно отдать статическому слою.
bool isHybrid(const char* type) {
    return strcmp(type, "gforce") == 0 || strcmp(type, "graph") == 0;
}

/**
 * Затирает ли виджет своё прошлое состояние сам.
 *
 * Такому виджету не нужно восстанавливать фон из статического слоя: он и так
 * закрашивает весь свой прямоугольник каждый кадр. Для шифт-дуги это половина
 * экрана (рамка 440x248) при том, что реально меняется цвет двух десятков
 * точек на неизменных позициях — восстановление там было чистой потерей
 * порядка 11 мс на кадр.
 *
 * Отправлять эти строки на панель по-прежнему нужно: пиксели меняются.
 *
 * Требование к виджету: за кадр он закрашивает каждый пиксель, который мог
 * закрасить в прошлом кадре. Полоса и руль сначала заливают дорожку во всю
 * рамку, шифт-лайт красит все ступени и все точки дуги, включая погасшие.
 * Незакрашенными остаются только зазоры и срезанные углы, а там лежит фон,
 * который не менялся.
 */
bool isSelfErasing(JsonObjectConst w, const char* type) {
    if (strcmp(type, "bar") == 0) return true;

    if (strcmp(type, "shift_light") == 0) {
        // flash — полноэкранный оверлей, у него своя ветка
        return strcmp(w["props"]["mode"] | "segments", "flash") != 0;
    }

    // У руля под полосой может стоять подпись с углом, а её область дорожкой
    // не закрывается — тогда без восстановления цифры наложились бы друг на
    // друга.
    if (strcmp(type, "steering") == 0) return !(w["props"]["showValue"] | false);

    return false;
}

/**
 * Область виджета, меняющаяся от кадра к кадру.
 *
 * Возвращает false, если виджет меняется целиком и надо брать всю рамку.
 *
 * Смысл есть только там, где рамка сильно больше движущейся части. Радар
 * G-force занимает 436x460 — 92% экрана, — но меняются в нём лишь шарик со
 * следом и цифры внизу. Восстановление и отправка всей рамки стоили около
 * 400 КБ трафика по PSRAM на кадр.
 *
 * Функция вызывается дважды за кадр — из restoreDynamic() до отрисовки и из
 * dirtyBands() после — и оба раза возвращает объединение области прошлого
 * кадра с текущей: прошлый кадр надо затереть, текущий отправить.
 */
bool liveRect(const Frame& f, JsonObjectConst w, const char* type, R& out) {
    if (strcmp(type, "gforce") != 0) return false;

    const R r = rectOf(f, w);
    const GForceGeom gm = gforceGeom(r);
    if (!gm.ok) return false;

    BallState* bs = ballState(w["id"] | "gforce");
    if (!bs || !bs->seen) return false;

    // Цифры внизу меняются каждый кадр, их полоса входит всегда
    R box{r.x, gm.numY - 2, r.w, gm.numPx + 4};

    // Шарик рисуется радиусом ballR+1, плюс запас на кайму
    const int pad = gm.ballR + 3;
    box = unite(box, R{bs->x - pad, bs->y - pad, pad * 2, pad * 2});

    box = clipTo(box, r);
    if (box.w <= 0) return false;

    out = unite(box, bs->live);
    bs->live = box;
    return true;
}

void paintWidget(const Frame& f, JsonObjectConst w, Part part = Part::All) {
    const R r = rectOf(f, w);
    if (r.w <= 0 || r.h <= 0) return;

    const char* type  = w["type"] | "";
    const char* sigId = w["signal"] | static_cast<const char*>(nullptr);
    const float value = sigId ? Signals::get(sigId) : 0.0f;

    if      (strcmp(type, "gforce")      == 0) { paintGForce(f, w, r, part); return; }
    else if (strcmp(type, "graph")       == 0) { paintGraph(f, w, r, part);  return; }

    // Остальные виджеты на части не делятся: либо неподвижны целиком, либо
    // меняются целиком. Их незачем вызывать в чужом проходе.
    if (part == Part::Chrome) return;

    if      (strcmp(type, "numeric")     == 0) paintNumeric(f, w, r, value);
    else if (strcmp(type, "label")       == 0) paintLabel(f, w, r);
    else if (strcmp(type, "bar")         == 0) paintBar(f, w, r, value);
    else if (strcmp(type, "shift_light") == 0) paintShiftLight(f, w, r, value);
    else if (strcmp(type, "warning")     == 0) paintWarning(f, w, r);
    else if (strcmp(type, "clock")       == 0) paintClock(f, w, r);
    else if (strcmp(type, "steering")    == 0) {
        // Отсутствующий сигнал = руль по центру. Общий fallback в 0 здесь не
        // подходит: 0 — это законное «полностью влево».
        paintSteering(f, w, r, sigId && Signals::has(sigId) ? value : 0.5f);
    }
    else if (strcmp(type, "tire_temp")   == 0) {
        // Сигналы не из w["signal"]: их двадцать, имена собираются из префикса
        paintTireTemp(f, w, r);
    }
    else paintPlaceholder(f, r, type);
}

} // namespace

// ─── Публичный вход ──────────────────────────────────────────────────────────

namespace {

constexpr int kMaxWidgets = 32;

/// Индексы виджетов экрана в порядке возрастания z.
/// Возвращает их количество, сам JSON не трогает.
int zOrderOf(JsonArrayConst ws, uint8_t* order) {
    const int n   = static_cast<int>(ws.size());
    const int cnt = n < kMaxWidgets ? n : kMaxWidgets;

    // Вставками: виджетов единицы, любая другая сортировка тут дороже себя.
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
    return cnt;
}

bool overlaps(const R& a, const R& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

/**
 * Раскладка экрана на слои: кто попадает в статический слой, а кто рисуется
 * каждый кадр.
 *
 * По типу виджета этого не решить до конца. Статический слой всегда лежит ПОД
 * динамическими виджетами, потому что фон под ними восстанавливается из слоя,
 * а сами они рисуются сверху. Значит подпись с z выше движущегося виджета, с
 * которым она перекрывается, в слое оставлять нельзя — иначе движущийся виджет
 * её закроет, и z-порядок перевернётся.
 *
 * Поэтому такие подписи понижаются до динамических. Проход идёт в порядке
 * отрисовки, и понижённый виджет дальше считается динамическим — так каскад
 * (подпись над подписью над баром) разрешается сам.
 */
struct Layers {
    uint8_t  order[kMaxWidgets];
    uint32_t dynamicMask = 0;   ///< бит по позиции в order
    /// Полноэкранные оверлеи по условию: Warning и вспышка шифт-лайта.
    ///
    /// Рисуются они в динамическом проходе, но в геометрию восстановления и
    /// отправки не входят. Их рамка — весь экран, а срабатывают они изредка;
    /// учитывать её постоянно значило бы каждый кадр копировать и отправлять
    /// полный кадр. Когда оверлей действительно нарисован, screen() сообщает
    /// об этом, и кадр восстанавливается целиком.
    uint32_t overlayMask = 0;
    /// Виджеты с неподвижной оправой: рисуются в оба прохода разными частями.
    uint32_t hybridMask = 0;
    int      count      = 0;
};

/// Кэш раскладки: пересчитывать её каждый кадр значило бы читать все рамки из
/// JSON заново, а меняется она только при смене экрана.
Layers  s_layers;
uint8_t s_layersScreen = 0xFF;

const Layers& layersOf(JsonArrayConst ws, uint8_t screenIdx) {
    if (s_layersScreen == screenIdx) return s_layers;

    Layers& L = s_layers;
    L.count       = zOrderOf(ws, L.order);
    L.dynamicMask = 0;
    L.overlayMask = 0;
    L.hybridMask  = 0;

    // Рамки в координатах лейаута, без сдвига сцены: сдвиг одинаков для всех
    // виджетов и на перекрытия не влияет.
    R rects[kMaxWidgets];
    for (int i = 0; i < L.count; ++i) {
        JsonObjectConst w  = ws[L.order[i]].as<JsonObjectConst>();
        JsonObjectConst rc = w["rect"];
        const char* type   = w["type"] | "";

        rects[i] = R{rc["x"] | 0, rc["y"] | 0, rc["w"] | 0, rc["h"] | 0};

        if (isFullScreen(w, type)) {
            L.dynamicMask |= (1u << i);
            L.overlayMask |= (1u << i);
            continue;
        }

        if (isHybrid(type)) L.hybridMask |= (1u << i);

        bool dyn = !isStatic(type);
        if (!dyn) {
            // Оверлеи в проверке не участвуют: их рамка накрывает экран, и
            // иначе они понижали бы в динамические каждую подпись над собой.
            const uint32_t below = L.dynamicMask & ~L.overlayMask;
            for (int j = 0; j < i; ++j) {
                if ((below & (1u << j)) && overlaps(rects[i], rects[j])) {
                    dyn = true;
                    break;
                }
            }
        }
        if (dyn) L.dynamicMask |= (1u << i);
    }

    // Печатается при смене экрана, то есть редко.
    //
    // Меряем ДОЛЮ СТРОК, попадающих в полосы отправки, а не сумму площадей
    // виджетов: виджеты перекрываются, и сумма площадей легко переваливает за
    // 100%, из-за чего это число сначала было бессмысленным. Отправка идёт
    // полосами во всю ширину, так что строки — это ровно то, за что мы платим.
    int dynCount = 0, ovCount = 0, hyCount = 0;
    for (int i = 0; i < L.count; ++i) {
        if (L.hybridMask  & (1u << i)) ++hyCount;
        if (L.overlayMask & (1u << i)) ++ovCount;
        else if (L.dynamicMask & (1u << i)) ++dynCount;
    }

    // Доля строк считается в dirtyBands() и печатается оттуда: у виджетов с
    // оправой живая область много меньше рамки, и оценка по рамкам врала —
    // радар G-force давал 95% при реальных единицах процентов.
    Serial.printf("[layers] экран %u: %d виджетов, %d динамических, "
                  "%d статических, %d оверлеев, %d с оправой\n",
                  screenIdx, L.count, dynCount,
                  L.count - dynCount - ovCount, ovCount, hyCount);

    s_layersScreen = screenIdx;
    return L;
}

} // namespace

bool screen(const Frame& f, uint8_t screenIdx, Pass pass) {
    JsonArrayConst ws = Layout::widgets(screenIdx);

    // Фон здесь НЕ заливается: во время свайпа в один кадр рисуются два
    // экрана со сдвигом, и заливка внутри этой функции стёрла бы первый.
    // Очистку делает вызывающий — см. loop() в main.cpp.

    // Оверлей определяется по факту заливки, а не по наличию виджета на
    // экране: Warning стоит почти всюду, но срабатывает изредка.
    s_overlayDrawn = false;

    if (pass == Pass::All) {
        uint8_t order[kMaxWidgets];
        const int cnt = zOrderOf(ws, order);
        for (int i = 0; i < cnt; ++i) {
            paintWidget(f, ws[order[i]].as<JsonObjectConst>());
        }
        s_layersScreen = 0xFF;   // порядок обхода не тот, кэш не строим
        return s_overlayDrawn;
    }

    const Layers& L = layersOf(ws, screenIdx);
    const bool wantDynamic = (pass == Pass::Dynamic);

    for (int i = 0; i < L.count; ++i) {
        JsonObjectConst w = ws[L.order[i]].as<JsonObjectConst>();

        // Гибрид попадает в оба прохода: оправа в статический слой, живая
        // часть поверх неё каждый кадр.
        if (L.hybridMask & (1u << i)) {
            paintWidget(f, w, wantDynamic ? Part::Live : Part::Chrome);
            continue;
        }

        if (((L.dynamicMask & (1u << i)) != 0) != wantDynamic) continue;
        paintWidget(f, w);
    }
    return s_overlayDrawn;
}

void restoreDynamic(const Frame& f, uint8_t screenIdx) {
    JsonArrayConst ws = Layout::widgets(screenIdx);
    const Layers& L   = layersOf(ws, screenIdx);

    for (int i = 0; i < L.count; ++i) {
        if (!(L.dynamicMask & (1u << i))) continue;
        if (L.overlayMask & (1u << i)) continue;

        JsonObjectConst w  = ws[L.order[i]].as<JsonObjectConst>();
        const char* type   = w["type"] | "";

        // Виджет, закрашивающий себя целиком, в восстановлении не участвует
        if (isSelfErasing(w, type)) continue;

        R r;
        if (!liveRect(f, w, type, r)) r = rectOf(f, w);
        if (r.w <= 0 || r.h <= 0) continue;

        // Запас в пиксель по каждой стороне: скруглённые рамки и толстые
        // линии кое-где выходят за геометрическую границу на полпикселя, и
        // без запаса от них остаётся кайма.
        Display::restoreRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2);
    }
}

int dirtyBands(const Frame& f, uint8_t screenIdx, Band* out, int max) {
    if (!out || max < 1) return 0;

    JsonArrayConst ws = Layout::widgets(screenIdx);
    const Layers& L   = layersOf(ws, screenIdx);

    Band raw[kMaxWidgets];
    int n = 0;
    for (int i = 0; i < L.count && n < kMaxWidgets; ++i) {
        if (!(L.dynamicMask & (1u << i))) continue;
        if (L.overlayMask & (1u << i)) continue;

        JsonObjectConst w = ws[L.order[i]].as<JsonObjectConst>();
        R r;
        if (!liveRect(f, w, w["type"] | "", r)) r = rectOf(f, w);
        if (r.h <= 0) continue;

        // Тот же запас в пиксель, что при восстановлении фона
        raw[n].y0 = static_cast<int16_t>(r.y - 1);
        raw[n].y1 = static_cast<int16_t>(r.y + r.h + 1);
        ++n;
    }
    if (n == 0) return 0;

    // Сортировка вставками по началу полосы: полос единицы
    for (int i = 1; i < n; ++i) {
        const Band v = raw[i];
        int j = i - 1;
        while (j >= 0 && raw[j].y0 > v.y0) { raw[j + 1] = raw[j]; --j; }
        raw[j + 1] = v;
    }

    // Слияние. Разрыв меньше kGap строк выгоднее передать вместе с полосой,
    // чем заводить под него отдельное окно адресации.
    constexpr int kGap = 12;
    int m = 0;
    out[0] = raw[0];
    for (int i = 1; i < n; ++i) {
        if (raw[i].y0 <= out[m].y1 + kGap) {
            if (raw[i].y1 > out[m].y1) out[m].y1 = raw[i].y1;
        } else if (m + 1 < max) {
            out[++m] = raw[i];
        } else {
            // Полосы кончились — остаток дотягиваем до последней.
            if (raw[i].y1 > out[m].y1) out[m].y1 = raw[i].y1;
        }
    }

    // Настоящая доля отправляемых строк, раз в секунду. Оценка по рамкам
    // виджетов, которая печаталась раньше в [layers], для виджетов с оправой
    // завышала её в разы: у радара рамка 92% экрана, а живая область — шарик.
    static uint32_t lastLogMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastLogMs >= 1000) {
        lastLogMs = nowMs;
        int rows = 0;
        for (int i = 0; i <= m; ++i) rows += out[i].y1 - out[i].y0;
        Serial.printf("[bands] экран %u: %d полос, отправка %d%% строк\n",
                      screenIdx, m + 1, rows * 100 / LCD_HEIGHT);
    }

    return m + 1;
}

} // namespace Render
