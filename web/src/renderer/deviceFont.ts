/**
 * Соответствие шрифтов редактора и устройства.
 *
 * Раньше здесь была «лестница размеров»: прошивка рисовала битовыми шрифтами,
 * глиф увеличивался только целым множителем, поэтому произвольный кегль был
 * недостижим и редактор обязан был прижимать значения к нескольким высотам.
 *
 * Теперь прошивка использует шрифты со сглаживанием (см. include/aa_font.h):
 * байт прозрачности на пиксель и дробный масштаб. Любой кегль воспроизводится
 * точно, так что лестница и прижимание размеров убраны — осталось только
 * согласование гарнитуры.
 *
 * ВАЖНО: гарнитура режима устройства должна совпадать с той, из которой
 * сгенерированы шрифты в tools/gen_font.py. Сейчас это Arial.
 */

/// Отношение высоты цифры к кеглю (em) у Arial/Helvetica.
/// Должно совпадать с Text::kCapPerEm в прошивке.
export const CAP_PER_EM = 0.716

// ── Режим устройства ────────────────────────────────────────────────────────

let deviceMode = false

export function setDeviceMode(on: boolean) { deviceMode = on }
export function isDeviceMode() { return deviceMode }

/**
 * Строка шрифта для canvas.
 *
 * В обычном режиме — гарнитуры, выбранные для редактора. В режиме устройства
 * подставляется Arial: прошивка растеризована именно из него, поэтому
 * пропорции и ширины букв совпадают с тем, что окажется на экране.
 * Моноширинный вариант в режиме устройства тоже становится Arial — отдельного
 * monospace в прошивке нет.
 */
export function fontOf(
  em: number,
  opts: { bold?: boolean; mono?: boolean } = {},
): string {
  const { bold = false, mono = false } = opts
  const weight = bold ? 'bold ' : ''

  if (!deviceMode) {
    const family = mono ? `'JetBrains Mono', monospace` : `Inter, sans-serif`
    return `${weight}${Math.round(em)}px ${family}`
  }

  return `${weight}${Math.round(em)}px Arial, Helvetica, sans-serif`
}

/**
 * Знак аварии: треугольник с восклицательным знаком.
 *
 * Рисуется примитивами, а не символом шрифта: во встроенных наборах прошивки
 * такого глифа нет. Парная реализация — drawWarnTriangle() в
 * src/widget_render.cpp.
 */
export function drawWarnTriangle(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  size: number,
  color: string,
) {
  const h = size
  const w = size * 1.15
  const lw = Math.max(2, size / 9)

  ctx.save()
  ctx.strokeStyle = color
  ctx.lineWidth = lw
  ctx.lineJoin = 'round'
  ctx.beginPath()
  ctx.moveTo(cx, cy - h / 2)
  ctx.lineTo(cx - w / 2, cy + h / 2)
  ctx.lineTo(cx + w / 2, cy + h / 2)
  ctx.closePath()
  ctx.stroke()

  ctx.fillStyle = color
  const barH = h / 3
  ctx.fillRect(cx - lw / 2, cy - barH / 3, lw, barH)
  ctx.fillRect(cx - lw / 2, cy + (barH * 2) / 3 + 2, lw, lw)
  ctx.restore()
}
