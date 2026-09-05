/**
 * Ограничения шрифтов устройства и режим «как на устройстве».
 *
 * Зачем это нужно. Прошивка рисует текст шрифтами FreeSans в формате GFXfont.
 * Глиф там — битмап фиксированного размера, увеличивать его можно только
 * целым множителем. Поэтому произвольный кегль на устройстве недостижим:
 * доступен лишь конечный набор высот. Если редактор позволяет выставить
 * что угодно, превью и железо неизбежно расходятся.
 *
 * Здесь лежит та же лестница размеров, что в src/text_render.cpp, и признак
 * режима устройства. Признак хранится модульной переменной, а не прокидывается
 * параметром через все painter-функции: отрисовка синхронная и однопоточная,
 * значение выставляется один раз на кадр в DisplayCanvas.
 *
 * ВАЖНО: при изменении набора шрифтов в прошивке править и BASE_CAPS здесь.
 */

/// Высота цифры '0' у базовых начертаний в прошивке, px.
/// Полужирные: FreeSansBold 9/12/18/24pt. Светлое: FreeSans 9pt.
const BASE_CAPS_BOLD  = [13, 17, 25, 35]
const BASE_CAPS_LIGHT = [13]

/// Прошивка не увеличивает глифы сильнее чем вчетверо.
const MAX_SCALE = 4

/// Отношение высоты цифры к кеглю (em). Должно совпадать с Text::kCapPerEm.
export const CAP_PER_EM = 0.72

function buildLadder(baseCaps: number[]): number[] {
  const out = new Set<number>()
  for (const cap of baseCaps) {
    for (let s = 1; s <= MAX_SCALE; s++) out.add(cap * s)
  }
  return [...out].sort((a, b) => a - b)
}

/// Достижимые высоты цифр, px.
export const CAP_LADDER_BOLD  = buildLadder(BASE_CAPS_BOLD)
export const CAP_LADDER_LIGHT = buildLadder(BASE_CAPS_LIGHT)

/// Те же ступени, но в кеглях (em) — в этих единицах живёт props.fontSize.
export const EM_LADDER_BOLD = CAP_LADDER_BOLD.map(c => Math.round(c / CAP_PER_EM))

// ── Режим устройства ────────────────────────────────────────────────────────

let deviceMode = false

export function setDeviceMode(on: boolean) { deviceMode = on }
export function isDeviceMode() { return deviceMode }

// ── Прижимание размеров ─────────────────────────────────────────────────────

/** Ближайшая достижимая высота цифры. */
export function snapCap(capPx: number, bold = true): number {
  const ladder = bold ? CAP_LADDER_BOLD : CAP_LADDER_LIGHT
  let best = ladder[0]
  for (const v of ladder) {
    if (Math.abs(v - capPx) < Math.abs(best - capPx)) best = v
  }
  return best
}

/** Ближайший достижимый кегль. Вне режима устройства возвращает как есть. */
export function snapEm(em: number, bold = true): number {
  if (!deviceMode) return em
  return snapCap(em * CAP_PER_EM, bold) / CAP_PER_EM
}

/**
 * Строка шрифта для canvas.
 *
 * В обычном режиме — гарнитуры, выбранные для редактора. В режиме устройства
 * подставляется Helvetica/Arial: FreeSans это её метрическая копия, поэтому
 * пропорции букв совпадают с тем, что нарисует прошивка. Моноширинный
 * вариант в режиме устройства тоже становится Helvetica — во прошивке
 * отдельного monospace нет.
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

  return `${weight}${Math.round(snapEm(em, bold))}px Helvetica, Arial, sans-serif`
}

/**
 * Знак аварии: треугольник с восклицательным знаком.
 *
 * Раньше здесь стоял emoji, но во встроенных шрифтах прошивки его нет и быть
 * не может. Поэтому иконка рисуется примитивами в обоих рендерах —
 * см. drawWarnTriangle() в src/widget_render.cpp.
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
