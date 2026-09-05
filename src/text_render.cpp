#include "text_render.h"

// Шрифты подключаются ТОЛЬКО здесь. Заголовки Adafruit объявляют массивы
// глифов без static, поэтому включение их в двух единицах трансляции даёт
// multiple definition на этапе линковки.
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "fonts/FreeSansBold18pt7b.h"
#include "fonts/FreeSansBold24pt7b.h"

namespace Text {

namespace {

/// Базовые шрифты по возрастанию размера.
const GFXfont* const kBold[]  = {&FreeSansBold9pt7b, &FreeSansBold12pt7b,
                                 &FreeSansBold18pt7b, &FreeSansBold24pt7b};
const GFXfont* const kLight[] = {&FreeSans9pt7b};

constexpr int kBoldCount  = sizeof(kBold) / sizeof(kBold[0]);
constexpr int kLightCount = sizeof(kLight) / sizeof(kLight[0]);

/// Увеличивать сильнее вчетверо бессмысленно: пиксельная лестница на краях
/// глифа становится заметнее самой буквы.
constexpr int kMaxScale = 4;

/// Глиф символа или nullptr, если символа в шрифте нет.
const GFXglyph* glyphOf(const GFXfont* font, char c) {
    const auto uc = static_cast<uint8_t>(c);
    if (!font || uc < font->first || uc > font->last) return nullptr;
    return font->glyph + (uc - font->first);
}

/// Высота цифры для базового шрифта.
///
/// Берётся из глифа '0': у FreeSans все цифры одной высоты, и это ровно тот
/// размер, который воспринимается как «размер шрифта» на приборной панели.
int baseCap(const GFXfont* font) {
    const GFXglyph* g = glyphOf(font, '0');
    return g ? g->height : 8;
}

} // namespace

int capPx(const Face& f) {
    if (!f.font) return 8 * (f.scale ? f.scale : 1);
    return baseCap(f.font) * (f.scale ? f.scale : 1);
}

Face pick(int targetCapPx, bool bold) {
    const GFXfont* const* list = bold ? kBold : kLight;
    const int count            = bold ? kBoldCount : kLightCount;

    if (targetCapPx < 1) targetCapPx = 1;

    Face best{list[0], 1};
    int  bestErr = 1 << 30;

    // Перебираем все комбинации «шрифт x множитель» и берём ближайшую по
    // высоте. При равной высоте выигрывает меньший множитель, потому что
    // крупный базовый шрифт всегда чище растянутого мелкого — поэтому
    // множитель во внутреннем цикле идёт по возрастанию, а строгое < не
    // позволяет более грубому варианту вытеснить уже найденный.
    for (int i = count - 1; i >= 0; --i) {
        const int cap = baseCap(list[i]);
        if (cap < 1) continue;
        for (int s = 1; s <= kMaxScale; ++s) {
            const int err = abs(cap * s - targetCapPx);
            if (err < bestErr) {
                bestErr = err;
                best    = Face{list[i], static_cast<uint8_t>(s)};
            }
        }
    }
    return best;
}

int ladder(int* out, int max, bool bold) {
    const GFXfont* const* list = bold ? kBold : kLight;
    const int count            = bold ? kBoldCount : kLightCount;

    int n = 0;
    for (int i = 0; i < count && n < max; ++i) {
        const int cap = baseCap(list[i]);
        for (int s = 1; s <= kMaxScale && n < max; ++s) {
            const int v = cap * s;
            // Отбрасываем дубликаты: 12pt x2 и 24pt x1 дают одну высоту
            bool dup = false;
            for (int k = 0; k < n; ++k) {
                if (out[k] == v) { dup = true; break; }
            }
            if (!dup) out[n++] = v;
        }
    }

    // Простая сортировка вставками — список короткий
    for (int i = 1; i < n; ++i) {
        const int v = out[i];
        int j = i - 1;
        while (j >= 0 && out[j] > v) { out[j + 1] = out[j]; --j; }
        out[j + 1] = v;
    }
    return n;
}

int width(const char* s, const Face& f) {
    if (!s || !f.font) return 0;
    int w = 0;
    for (const char* p = s; *p; ++p) {
        const GFXglyph* g = glyphOf(f.font, *p);
        if (g) w += g->xAdvance;
    }
    return w * (f.scale ? f.scale : 1);
}

Face fit(const char* s, int maxW, int maxH, bool bold) {
    if (maxW < 1) maxW = 1;
    if (maxH < 1) maxH = 1;

    // Стартуем от ограничения по высоте и спускаемся по лестнице, пока
    // строка не влезет по ширине.
    int steps[32];
    const int n = ladder(steps, 32, bold);

    Face chosen = pick(steps[0], bold);
    for (int i = n - 1; i >= 0; --i) {
        if (steps[i] > maxH) continue;
        const Face cand = pick(steps[i], bold);
        if (width(s, cand) <= maxW) return cand;
        chosen = cand;
    }
    // Ни один вариант не влез по ширине — отдаём самый мелкий
    return pick(steps[0], bold);
}

void drawBox(Arduino_GFX* g, const char* s,
             int bx, int by, int bw, int bh,
             HA ha, VA va, const Face& f, uint16_t color) {
    if (!g || !s || !*s || !f.font) return;

    g->setFont(f.font);
    g->setTextSize(f.scale ? f.scale : 1);
    g->setTextColor(color);

    const int tw  = width(s, f);
    const int cap = capPx(f);

    int x = bx;
    if (ha == HA::Center)     x = bx + (bw - tw) / 2;
    else if (ha == HA::Right) x = bx + bw - tw;

    // Курсор у GFXfont стоит на базовой линии, а не на верхнем краю
    int baseline = by + cap;
    if (va == VA::Mid)         baseline = by + (bh + cap) / 2;
    else if (va == VA::Bottom) baseline = by + bh;

    g->setCursor(x, baseline);
    g->print(s);
}

void drawCentered(Arduino_GFX* g, const char* s, int cx, int cy,
                  const Face& f, uint16_t color) {
    if (!g || !s || !*s || !f.font) return;

    g->setFont(f.font);
    g->setTextSize(f.scale ? f.scale : 1);
    g->setTextColor(color);

    const int tw  = width(s, f);
    const int cap = capPx(f);

    g->setCursor(cx - tw / 2, cy + cap / 2);
    g->print(s);
}

void useBuiltin(Arduino_GFX* g) {
    if (g) g->setFont(nullptr);
}

} // namespace Text
