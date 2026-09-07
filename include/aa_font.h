#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Сглаженные шрифты: формат данных и вывод в фреймбуфер.
 *
 * Отличие от GFXfont: глиф хранится по байту на пиксель, и байт — это
 * прозрачность. Поэтому у краёв есть полутона, и текст не рассыпается на
 * квадраты при увеличении. Второе: масштаб задаётся дробным числом, а не
 * целым множителем, так что любой кегль из редактора воспроизводится точно.
 *
 * Данные генерируются tools/gen_font.py и лежат в include/fonts/Aa*.h.
 * Массивы объявлены const, то есть попадают в .rodata и читаются прямо из
 * флеша через отображение в память — оперативную память они не занимают.
 */

/// Один глиф. Смещения отсчитываются от курсора на базовой линии.
struct AaGlyph {
    uint32_t off;    ///< Смещение битмапа в массиве alpha
    uint8_t  w;      ///< Ширина битмапа, px
    uint8_t  h;      ///< Высота битмапа, px
    int8_t   dx;     ///< Сдвиг левого края от курсора
    int16_t  dy;     ///< Сдвиг верхнего края от базовой линии (обычно < 0)
    uint16_t adv16;  ///< Шаг курсора в 1/16 px
};

struct AaFont {
    const uint8_t* alpha;      ///< Все битмапы подряд, 8 бит на пиксель
    const AaGlyph* glyphs;     ///< Глифы в порядке возрастания кода
    const uint8_t* map;        ///< Код символа -> индекс в glyphs, 0xFF = нет
    uint8_t        mapFirst;   ///< Код первого символа в map
    uint8_t        mapLast;    ///< Код последнего символа в map
    uint16_t       capHeight;  ///< Высота цифры '0' в базовом кегле, px
    uint16_t       lineHeight; ///< Ascent + descent базового кегля, px
    uint16_t       ascent;     ///< Ascent базового кегля, px
};

namespace Aa {

/// Масштаб хранится в формате 16.16: 65536 = натуральная величина.
constexpr int32_t kOne = 1 << 16;

/// Масштаб, при котором высота цифры станет равна capPx.
inline int32_t scaleForCap(const AaFont& f, int capPx) {
    if (f.capHeight == 0) return kOne;
    return static_cast<int32_t>(
        (static_cast<int64_t>(capPx) * kOne) / f.capHeight);
}

/// Высота цифры при заданном масштабе.
inline int capAt(const AaFont& f, int32_t scale) {
    return static_cast<int>((static_cast<int64_t>(f.capHeight) * scale) >> 16);
}

/// Ширина строки в пикселях при заданном масштабе.
int width(const AaFont& f, const char* s, int32_t scale);

/**
 * Вывод строки прямо в фреймбуфер с альфа-смешиванием.
 *
 * Пишем в буфер сами, минуя Arduino_GFX: смешивание требует ЧТЕНИЯ пикселя
 * фона, а публичного быстрого чтения у Arduino_GFX нет — только попиксельная
 * запись через виртуальный вызов. Прямой доступ к буферу в PSRAM и дешевле,
 * и даёт корректное наложение на то, что уже нарисовано под текстом.
 *
 * penX/baselineY — курсор на базовой линии. Клиппинг по границам буфера.
 */
void drawString(uint16_t* fb, int fbW, int fbH,
                const AaFont& f, const char* s,
                int penX, int baselineY, int32_t scale, uint16_t color);

} // namespace Aa
