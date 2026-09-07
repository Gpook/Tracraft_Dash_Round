#include "text_render.h"

#include "display_hw.h"
#include "pins.h"

// Шрифты подключаются ТОЛЬКО здесь: массивы объявлены без static-обёртки в
// пространстве имён, и включение этих заголовков в двух единицах трансляции
// дало бы дублирование данных во флеше.
#include "fonts/AaBoldBig.h"
#include "fonts/AaBoldCaps.h"
#include "fonts/AaBoldText.h"
#include "fonts/AaRegularText.h"

namespace Text {

namespace {

/// Полужирные начерки по убыванию алфавита: цифры, прописные, весь ASCII.
/// Чем крупнее начерк, тем беднее алфавит — см. FONTS в tools/gen_font.py.
const AaFont* const kBoldChain[] = {&AaBoldBig, &AaBoldCaps, &AaBoldText};
constexpr int kBoldCount = sizeof(kBoldChain) / sizeof(kBoldChain[0]);

/// Есть ли в начерке глифы для всех символов строки.
///
/// Пробел и знак 0xC2 из UTF-8-последовательности градуса пропускаются: их
/// растеризатор всё равно не рисует, и требовать для них глиф значило бы без
/// причины отбрасывать подходящий начерк.
bool covers(const AaFont& f, const char* s) {
    if (!s) return true;
    for (const char* p = s; *p; ++p) {
        const auto uc = static_cast<uint8_t>(*p);
        if (uc == ' ' || uc == 0xC2) continue;
        if (uc < f.mapFirst || uc > f.mapLast) return false;
        if (f.map[uc - f.mapFirst] == 0xFF) return false;
    }
    return true;
}

} // namespace

int capPx(const Face& f) {
    if (!f.font) return 0;
    return Aa::capAt(*f.font, f.scale);
}

Face pick(const char* s, int targetCapPx, bool bold) {
    if (targetCapPx < 1) targetCapPx = 1;

    if (!bold) {
        return Face{&AaRegularText, Aa::scaleForCap(AaRegularText, targetCapPx)};
    }

    // Идём от мелкого к крупному и берём первый подходящий. Мелкий выгоднее
    // вдвойне: уменьшение читает исходные пиксели, поэтому сжимать начерк
    // 96px до 20px значило бы перебирать 6 КБ на каждый символ подписи.
    const AaFont* chosen = nullptr;
    for (int i = kBoldCount - 1; i >= 0; --i) {
        const AaFont* f = kBoldChain[i];
        if (!covers(*f, s)) continue;
        chosen = f;
        if (static_cast<int>(f->capHeight) >= targetCapPx) break;
    }

    // Покрывающего начерка не нашлось вовсе — строка из символов, которых нет
    // ни в одном наборе. Рисуем самым полным: часть глифов пропадёт, но это
    // лучше пустого места, и видно, что данные пришли неожидаемые.
    if (!chosen) chosen = &AaBoldText;

    return Face{chosen, Aa::scaleForCap(*chosen, targetCapPx)};
}

int width(const char* s, const Face& f) {
    if (!s || !f.font) return 0;
    return Aa::width(*f.font, s, f.scale);
}

Face fit(const char* s, int maxW, int maxH, bool bold) {
    if (maxW < 1) maxW = 1;
    if (maxH < 1) maxH = 1;

    // Сначала упираемся в высоту, затем, если не влезли по ширине, уменьшаем
    // пропорционально. Второй проход нужен потому, что уменьшение может
    // перевести на другой базовый начерк с иными пропорциями.
    int cap = maxH;
    for (int pass = 0; pass < 2; ++pass) {
        const Face cand = pick(s, cap, bold);
        const int  w    = width(s, cand);
        if (w <= maxW || w == 0) return cand;
        cap = (cap * maxW) / w;
        if (cap < 1) cap = 1;
    }
    return pick(s, cap, bold);
}

void drawBox(Arduino_GFX* g, const char* s,
             int bx, int by, int bw, int bh,
             HA ha, VA va, const Face& f, uint16_t color) {
    if (!g || !s || !*s || !f.font) return;

    uint16_t* fb = Display::framebuffer();
    if (!fb) return;

    const int tw  = width(s, f);
    const int cap = capPx(f);

    int x = bx;
    if (ha == HA::Center)     x = bx + (bw - tw) / 2;
    else if (ha == HA::Right) x = bx + bw - tw;

    // Базовая линия ставится по высоте цифр, а не по межстрочному интервалу
    int baseline = by + cap;
    if (va == VA::Mid)         baseline = by + (bh + cap) / 2;
    else if (va == VA::Bottom) baseline = by + bh;

    Aa::drawString(fb, LCD_WIDTH, LCD_HEIGHT, *f.font, s, x, baseline,
                   f.scale, color);
}

void drawCentered(Arduino_GFX* g, const char* s, int cx, int cy,
                  const Face& f, uint16_t color) {
    if (!g || !s || !*s || !f.font) return;

    uint16_t* fb = Display::framebuffer();
    if (!fb) return;

    const int tw  = width(s, f);
    const int cap = capPx(f);

    Aa::drawString(fb, LCD_WIDTH, LCD_HEIGHT, *f.font, s,
                   cx - tw / 2, cy + cap / 2, f.scale, color);
}

void useBuiltin(Arduino_GFX* g) {
    if (g) {
        g->setFont(nullptr);
        g->setTextSize(2);
    }
}

} // namespace Text
