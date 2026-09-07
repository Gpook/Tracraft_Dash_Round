#include "aa_font.h"

namespace Aa {

namespace {

/// Глиф символа или nullptr, если его нет в наборе.
///
/// Байт 0xC2 пропускается специально. Единицы измерения приходят из лейаута в
/// UTF-8, и знак градуса там закодирован парой 0xC2 0xB0. Полноценный разбор
/// UTF-8 здесь не нужен: во всём наборе символов прошивки многобайтный только
/// градус, поэтому достаточно проглотить ведущий байт и нарисовать 0xB0.
const AaGlyph* glyphOf(const AaFont& f, char c) {
    const auto uc = static_cast<uint8_t>(c);
    if (uc == 0xC2) return nullptr;
    if (uc < f.mapFirst || uc > f.mapLast) return nullptr;
    const uint8_t idx = f.map[uc - f.mapFirst];
    if (idx == 0xFF) return nullptr;
    return &f.glyphs[idx];
}

/// Смешивание одного канала. Деление на 255 заменено сдвигами: точность до
/// единицы младшего разряда, а делений на пиксель тут три.
inline uint32_t mix(uint32_t src, uint32_t dst, uint32_t a) {
    const uint32_t t = src * a + dst * (255u - a) + 128u;
    return (t + (t >> 8)) >> 8;
}

} // namespace

int width(const AaFont& f, const char* s, int32_t scale) {
    if (!s) return 0;

    // Курсор в 16.16, чтобы дробные шаги не накапливали ошибку по строке
    int64_t pen = 0;
    for (const char* p = s; *p; ++p) {
        const AaGlyph* g = glyphOf(f, *p);
        if (!g) continue;
        pen += (static_cast<int64_t>(g->adv16) * scale) / 16;
    }
    return static_cast<int>(pen >> 16);
}

void drawString(uint16_t* fb, int fbW, int fbH,
                const AaFont& f, const char* s,
                int penX, int baselineY, int32_t scale, uint16_t color) {
    if (!fb || !s || scale <= 0) return;

    // Обратный масштаб для выборки: сколько исходных пикселей приходится на
    // один выходной. Тоже 16.16.
    const int32_t inv = static_cast<int32_t>(
        (static_cast<int64_t>(kOne) << 16) / scale);

    const uint32_t sr = (color >> 11) & 0x1F;
    const uint32_t sg = (color >> 5) & 0x3F;
    const uint32_t sb = color & 0x1F;

    int64_t pen = static_cast<int64_t>(penX) << 16;

    for (const char* p = s; *p; ++p) {
        const AaGlyph* g = glyphOf(f, *p);
        if (!g) continue;

        const int64_t adv = (static_cast<int64_t>(g->adv16) * scale) / 16;
        if (g->w == 0 || g->h == 0) {  // пробел
            pen += adv;
            continue;
        }

        const int gx = static_cast<int>(pen >> 16) +
                       static_cast<int>((static_cast<int64_t>(g->dx) * scale) >> 16);
        const int gy = baselineY +
                       static_cast<int>((static_cast<int64_t>(g->dy) * scale) >> 16);

        int dw = static_cast<int>((static_cast<int64_t>(g->w) * scale) >> 16);
        int dh = static_cast<int>((static_cast<int64_t>(g->h) * scale) >> 16);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;

        const uint8_t* src = f.alpha + g->off;

        for (int j = 0; j < dh; ++j) {
            const int y = gy + j;
            if (y < 0 || y >= fbH) continue;

            // Выходной пиксель усредняется по прямоугольнику исходных. При
            // уменьшении это подавляет ступеньки, при масштабе около единицы
            // прямоугольник схлопывается в один пиксель — и это нормально:
            // исходник уже сглажен, полутона никуда не деваются.
            int sy0 = static_cast<int>((static_cast<int64_t>(j) * inv) >> 16);
            int sy1 = static_cast<int>((static_cast<int64_t>(j + 1) * inv) >> 16);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sy1 > g->h) sy1 = g->h;
            if (sy0 >= g->h) continue;

            uint16_t* row = fb + static_cast<size_t>(y) * fbW;

            for (int i = 0; i < dw; ++i) {
                const int x = gx + i;
                if (x < 0 || x >= fbW) continue;

                int sx0 = static_cast<int>((static_cast<int64_t>(i) * inv) >> 16);
                int sx1 = static_cast<int>((static_cast<int64_t>(i + 1) * inv) >> 16);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sx1 > g->w) sx1 = g->w;
                if (sx0 >= g->w) continue;

                uint32_t sum = 0;
                uint32_t cnt = 0;
                for (int sy = sy0; sy < sy1; ++sy) {
                    const uint8_t* line = src + static_cast<size_t>(sy) * g->w;
                    for (int sx = sx0; sx < sx1; ++sx) sum += line[sx];
                    cnt += static_cast<uint32_t>(sx1 - sx0);
                }
                if (cnt == 0) continue;

                const uint32_t a = sum / cnt;
                if (a == 0) continue;

                if (a >= 254) {
                    row[x] = color;
                    continue;
                }

                const uint16_t d = row[x];
                const uint32_t r = mix(sr, (d >> 11) & 0x1F, a);
                const uint32_t gg = mix(sg, (d >> 5) & 0x3F, a);
                const uint32_t b = mix(sb, d & 0x1F, a);
                row[x] = static_cast<uint16_t>((r << 11) | (gg << 5) | b);
            }
        }

        pen += adv;
    }
}

} // namespace Aa
