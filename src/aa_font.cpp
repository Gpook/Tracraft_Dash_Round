#include "aa_font.h"
#include "gfx_util.h"

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

    // При увеличении и масштабе около единицы усреднять нечего: на выходной
    // пиксель приходится не больше одного исходного. Это основной случай для
    // крупных цифр, и для него есть отдельный цикл без деления.
    const bool upscale = inv <= static_cast<int32_t>(kOne);

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

        // Отсечение по кадру считается один раз на глиф.
        //
        // Раньше границы проверялись на каждом пикселе, а позиция в исходнике
        // считалась как (i * inv) >> 16 в 64 битах — тоже на каждом пикселе.
        // На Xtensa 64-битное умножение это несколько инструкций, и на крупных
        // цифрах набегали миллисекунды. Здесь вместо умножения аккумулятор.
        int i0 = gx < 0 ? -gx : 0;
        int i1 = (gx + dw > fbW) ? (fbW - gx) : dw;
        int j0 = gy < 0 ? -gy : 0;
        int j1 = (gy + dh > fbH) ? (fbH - gy) : dh;
        if (i1 <= i0 || j1 <= j0) { pen += adv; continue; }

        // Позиция в исходнике, 16.16. Оценка сверху: dh и dw не больше высоты
        // панели, inv при разумных масштабах в пределах десятков тысяч, так
        // что произведение укладывается в 32 бита с большим запасом.
        uint32_t yAcc = static_cast<uint32_t>(j0) * static_cast<uint32_t>(inv);

        for (int j = j0; j < j1; ++j, yAcc += static_cast<uint32_t>(inv)) {
            int sy0 = static_cast<int>(yAcc >> 16);
            if (sy0 >= g->h) break;

            uint16_t* row = fb + static_cast<size_t>(gy + j) * fbW + gx;
            uint32_t xAcc = static_cast<uint32_t>(i0) * static_cast<uint32_t>(inv);

            if (upscale) {
                // Выборка один-в-один: прямоугольник усреднения схлопнут в
                // один пиксель, поэтому ни суммы, ни деления не нужно.
                // Исходник уже сглажен, полутона никуда не деваются.
                const uint8_t* line = src + static_cast<size_t>(sy0) * g->w;

                for (int i = i0; i < i1; ++i, xAcc += static_cast<uint32_t>(inv)) {
                    const int sx = static_cast<int>(xAcc >> 16);
                    if (sx >= g->w) break;

                    const uint32_t a = line[sx];
                    if (a == 0) continue;
                    // Фреймбуфер хранит пиксели pre-swapped (big-endian).
                    // При полном перекрытии просто пишем swap16(color).
                    if (a >= 254) { row[i] = swap16(color); continue; }

                    // Разворачиваем фон обратно в native для blend, результат
                    // снова переставляем перед записью.
                    const uint16_t d = swap16(row[i]);
                    row[i] = swap16(static_cast<uint16_t>(
                        (mix(sr, (d >> 11) & 0x1F, a) << 11) |
                        (mix(sg, (d >> 5)  & 0x3F, a) << 5)  |
                         mix(sb,  d        & 0x1F, a)));
                }
                continue;
            }

            // Уменьшение: выходной пиксель усредняется по прямоугольнику
            // исходных, иначе на мелком кегле лезут ступеньки.
            int sy1 = static_cast<int>((yAcc + inv) >> 16);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sy1 > g->h) sy1 = g->h;

            for (int i = i0; i < i1; ++i, xAcc += static_cast<uint32_t>(inv)) {
                int sx0 = static_cast<int>(xAcc >> 16);
                if (sx0 >= g->w) break;
                int sx1 = static_cast<int>((xAcc + inv) >> 16);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sx1 > g->w) sx1 = g->w;

                const int bw = sx1 - sx0;
                uint32_t sum = 0;
                for (int sy = sy0; sy < sy1; ++sy) {
                    const uint8_t* line = src + static_cast<size_t>(sy) * g->w;
                    for (int sx = sx0; sx < sx1; ++sx) sum += line[sx];
                }

                const uint32_t a = sum / static_cast<uint32_t>(bw * (sy1 - sy0));
                if (a == 0) continue;
                if (a >= 254) { row[i] = swap16(color); continue; }

                const uint16_t d = swap16(row[i]);
                row[i] = swap16(static_cast<uint16_t>(
                    (mix(sr, (d >> 11) & 0x1F, a) << 11) |
                    (mix(sg, (d >> 5)  & 0x3F, a) << 5)  |
                     mix(sb,  d        & 0x1F, a)));
            }
        }

        pen += adv;
    }
}

} // namespace Aa
