#!/usr/bin/env python3
"""
Генератор сглаженных шрифтов для прошивки.

Зачем. Встроенные в Arduino_GFX шрифты (и формат GFXfont от Adafruit) хранят
глиф как БИТОВУЮ маску: пиксель либо есть, либо нет. Увеличивать такой глиф
можно только целым множителем, и на 466x466 крупные цифры выглядят как
восьмибитная графика — именно на это жаловался пользователь. Полутонов у краёв
нет физически, поэтому «подкрутить» существующий формат нельзя.

Здесь глиф растеризуется в 8 бит на пиксель: значение — прозрачность (0..255).
При выводе прошивка смешивает цвет текста с фоном по этой прозрачности, и края
получаются такими же гладкими, как в canvas редактора.

Второе следствие формата: масштаб больше не обязан быть целым. Один крупный
базовый начерк ужимается до любого нужного размера усреднением по площади, то
есть лестница размеров в редакторе больше не нужна.

Использование:
    python tools/gen_font.py

Требует Pillow. Пути к TTF задаются в FONTS ниже.

О лицензиях. По умолчанию берётся Arial из C:\\Windows\\Fonts — он метрически
совпадает с Helvetica, которую редактор использует в режиме «как на устройстве»,
так что превью и железо сходятся точно. Arial проприетарный: если проект станет
публичным, замените путь на шрифт с открытой лицензией (Inter, Roboto,
DejaVu Sans) — формат и генератор от этого не меняются.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Нужен Pillow: python -m pip install pillow")


ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "include" / "fonts"

# Отступ вокруг глифа при растеризации, чтобы ничего не срезалось
PAD = 16

# Знак градуса приходит из лейаута в UTF-8 (0xC2 0xB0). Растеризатор прошивки
# индексирует глифы одним байтом, поэтому 0xC2 он пропускает, а 0xB0 ищет
# здесь — см. glyphOf() в src/aa_font.cpp.
DEGREE = chr(0xB0)

SYMBOLS = "0123456789.:+-/% " + DEGREE
UPPER = "".join(chr(c) for c in range(ord("A"), ord("Z") + 1))
ASCII = "".join(chr(c) for c in range(32, 127)) + DEGREE


@dataclass
class FontSpec:
    name: str          # имя в C++
    ttf: str           # путь к TTF
    cap: int           # желаемая высота цифры '0' в пикселях
    charset: str
    comment: str


# Набор устроен лестницей: чем крупнее начерк, тем беднее его алфавит.
# Причина в цене. Глиф хранится байт на пиксель, поэтому вес растёт как
# квадрат кегля: полный ASCII с высотой цифры 96px занял бы больше полумегабайта
# при нулевой пользе — строчные буквы такого размера на приборной панели не
# встречаются. Выбор начерка под конкретную строку делает Text::pick().
FONTS = [
    FontSpec(
        name="AaBoldBig",
        ttf=r"C:\Windows\Fonts\arialbd.ttf",
        cap=96,
        charset=SYMBOLS,
        comment="Крупные числа во весь экран: скорость, обороты.",
    ),
    FontSpec(
        name="AaBoldCaps",
        ttf=r"C:\Windows\Fonts\arialbd.ttf",
        cap=64,
        charset=UPPER + SYMBOLS,
        comment=(
            "Крупные надписи прописными: BRAKE, ACCEL, COOLANT.\n"
            "Строчных нет — на приборной панели подписи набирают капителью."
        ),
    ),
    FontSpec(
        name="AaBoldText",
        ttf=r"C:\Windows\Fonts\arialbd.ttf",
        cap=32,
        charset=ASCII,
        comment="Полужирный ASCII: мелкие подписи и единицы измерения.",
    ),
    FontSpec(
        name="AaRegularText",
        ttf=r"C:\Windows\Fonts\arial.ttf",
        cap=32,
        charset=ASCII,
        comment="Светлый ASCII: второстепенные подписи, где полужирный давит.",
    ),
]


def em_for_cap(ttf: str, target_cap: int) -> tuple[ImageFont.FreeTypeFont, int]:
    """Подбор кегля, при котором высота цифры '0' равна target_cap.

    Прямой пропорции нет: FreeType хинтует контуры под конкретный размер, из-за
    чего высота цифры прыгает на пиксель туда-сюда. Поэтому идём от оценки и
    уточняем перебором.
    """
    em = max(4, int(target_cap / 0.716))
    best, best_err = em, 1 << 30
    for cand in range(max(4, em - 6), em + 7):
        f = ImageFont.truetype(ttf, cand)
        box = f.getbbox("0")
        cap = box[3] - box[1]
        err = abs(cap - target_cap)
        if err < best_err:
            best, best_err = cand, err
    return ImageFont.truetype(ttf, best), best


@dataclass
class Glyph:
    ch: str
    w: int
    h: int
    dx: int
    dy: int
    adv16: int
    data: bytes


def raster(font: ImageFont.FreeTypeFont, ch: str) -> Glyph:
    ascent, descent = font.getmetrics()
    box = font.getbbox(ch)

    cw = (box[2] - box[0]) + PAD * 2 + 8
    chh = ascent + descent + PAD * 2 + 8
    img = Image.new("L", (max(cw, 8), max(chh, 8)), 0)
    draw = ImageDraw.Draw(img)

    # anchor="ls": курсор в левой точке базовой линии. Так смещения глифа
    # считаются относительно базовой линии, как этого ждёт растеризатор прошивки.
    baseline = PAD + ascent
    draw.text((PAD, baseline), ch, font=font, fill=255, anchor="ls")

    tight = img.getbbox()
    adv16 = int(round(font.getlength(ch) * 16))

    if tight is None:  # пробел и прочие пустые глифы
        return Glyph(ch, 0, 0, 0, 0, adv16, b"")

    x0, y0, x1, y1 = tight
    crop = img.crop(tight)
    return Glyph(
        ch=ch,
        w=x1 - x0,
        h=y1 - y0,
        dx=x0 - PAD,
        dy=y0 - baseline,
        adv16=adv16,
        data=crop.tobytes(),
    )


def emit(spec: FontSpec) -> tuple[str, int]:
    font, em = em_for_cap(spec.ttf, spec.cap)
    ascent, descent = font.getmetrics()
    cap = font.getbbox("0")[3] - font.getbbox("0")[1]

    glyphs = [raster(font, ch) for ch in spec.charset]

    # Таблица ASCII -> индекс. Набор символов может быть разреженным
    # (у крупного начерка это только цифры), поэтому индексируем через карту,
    # а не через диапазон first..last.
    codes = sorted(ord(g.ch) for g in glyphs)
    first, last = codes[0], codes[-1]
    index = {ord(g.ch): i for i, g in enumerate(glyphs)}
    table = [index.get(c, 0xFF) for c in range(first, last + 1)]

    blob = bytearray()
    offsets = []
    for g in glyphs:
        offsets.append(len(blob))
        blob += g.data

    lines: list[str] = []
    ap = lines.append
    ap("// СГЕНЕРИРОВАНО tools/gen_font.py — руками не править.")
    ap("//")
    for line in spec.comment.splitlines():
        ap(f"// {line}".rstrip())
    ap("//")
    ap(f"// Начерк: {Path(spec.ttf).name}, кегль {em}px, высота цифры {cap}px")
    ap(f"// Символов: {len(glyphs)}, данных: {len(blob) / 1024:.1f} КБ")
    ap("")
    ap("#pragma once")
    ap("")
    ap('#include "aa_font.h"')
    ap("")

    ap(f"static const uint8_t {spec.name}_alpha[] = {{")
    for i in range(0, len(blob), 32):
        chunk = ",".join(str(b) for b in blob[i:i + 32])
        ap(f"    {chunk},")
    ap("};")
    ap("")

    ap(f"static const AaGlyph {spec.name}_glyphs[] = {{")
    for g, off in zip(glyphs, offsets):
        shown = g.ch if g.ch.isprintable() and g.ch != " " else "SP"
        ap(f"    {{{off:6d}, {g.w:3d}, {g.h:3d}, {g.dx:4d}, {g.dy:4d}, "
           f"{g.adv16:5d}}},  // '{shown}'")
    ap("};")
    ap("")

    ap(f"static const uint8_t {spec.name}_map[] = {{")
    for i in range(0, len(table), 24):
        chunk = ",".join(f"{v:3d}" for v in table[i:i + 24])
        ap(f"    {chunk},")
    ap("};")
    ap("")

    ap(f"static const AaFont {spec.name} = {{")
    ap(f"    {spec.name}_alpha,")
    ap(f"    {spec.name}_glyphs,")
    ap(f"    {spec.name}_map,")
    ap(f"    {first}, {last},")
    ap(f"    {cap}, {ascent + descent}, {ascent},")
    ap("};")
    ap("")

    path = OUT_DIR / f"{spec.name}.h"
    path.write_text("\n".join(lines), encoding="utf-8")
    return spec.name, len(blob)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    total = 0
    for spec in FONTS:
        if not Path(spec.ttf).exists():
            sys.exit(f"Не найден шрифт: {spec.ttf}")
        name, size = emit(spec)
        print(f"{name:16s} {size / 1024:7.1f} KB  -> include/fonts/{name}.h")
        total += size
    print(f"{'TOTAL':16s} {total / 1024:7.1f} KB flash")


if __name__ == "__main__":
    main()
