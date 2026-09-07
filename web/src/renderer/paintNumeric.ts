/**
 * Numeric — цифровое значение с подписью.
 *
 * Размер шрифта:
 *   props.fontSize > 0  → фиксированный размер
 *   иначе               → автоподгонка под rect (по высоте И ширине)
 *
 * Caption и unit масштабируются пропорционально основному значению,
 * поэтому виджет целиком растёт вслед за рамкой.
 */

import { NumericWidget, zoneColor } from '@/schema/layout'
import { fitFontSize } from './fitText'
import { fontOf } from './deviceFont'

// Доли высоты rect, отводимые под caption и unit при автоподгонке
const CAPTION_BAND = 0.22
const UNIT_BAND    = 0.20
// Небольшой запас, чтобы текст не касался краёв рамки
const FIT_PADDING  = 0.94

export function paintNumeric(
  ctx: CanvasRenderingContext2D,
  w: NumericWidget,
  value: number,
  unit: string,
  theme: { fg: string; muted: string },
  _time: number,
) {
  const { rect, props } = w
  const {
    decimals = 0,
    align = 'center',
    color,
    fontSize,
    showUnit = true,
    caption,
    colorFromZones,
    zones,
    staleAfterMs: _stale = 2000,
  } = props

  const valueText = isNaN(value) ? '–' : value.toFixed(decimals)
  const hasCaption = Boolean(caption)
  const hasUnit    = Boolean(showUnit && unit)

  ctx.save()
  ctx.translate(rect.x, rect.y)

  // ─── Цвет значения ────────────────────────────────────────────────────────
  let valColor = (color ?? theme.fg) as string
  if (colorFromZones && zones) {
    valColor = zoneColor(zones, value, valColor as `#${string}`)
  }

  // ─── Размеры шрифтов ──────────────────────────────────────────────────────
  // В режиме устройства fontOf() подменяет гарнитуру на Arial — именно из него
  // растеризованы шрифты прошивки, поэтому ширины строк совпадают.
  const valueFontTpl = (px: number) => fontOf(px, { bold: true, mono: true })
  const subFontTpl   = (px: number) => fontOf(px)

  let valueSize: number
  if (fontSize && fontSize > 0) {
    // Явно заданный размер — уважаем как есть
    valueSize = fontSize
  } else {
    // Автоподгонка: делим высоту между caption / значением / unit
    const captionBand = hasCaption ? rect.h * CAPTION_BAND : 0
    const unitBand    = hasUnit    ? rect.h * UNIT_BAND    : 0
    const availH      = Math.max(1, rect.h - captionBand - unitBand)
    valueSize = fitFontSize(ctx, valueText, rect.w * FIT_PADDING, availH * FIT_PADDING, valueFontTpl)
  }

  // Подписи — пропорция от основного размера, но не крупнее своей полосы
  const subSize = Math.max(7, Math.round(valueSize * 0.30))
  const captionH = hasCaption ? subSize * 1.35 : 0
  const unitH    = hasUnit    ? subSize * 1.30 : 0

  const ax = align === 'center' ? rect.w / 2 : align === 'right' ? rect.w : 0

  // ─── Caption — сверху ─────────────────────────────────────────────────────
  if (hasCaption) {
    ctx.font = subFontTpl(subSize)
    ctx.fillStyle = theme.muted
    ctx.textAlign = align
    ctx.textBaseline = 'top'
    ctx.fillText(caption as string, ax, 0)
  }

  // ─── Значение — центрируется в остатке ────────────────────────────────────
  ctx.font = valueFontTpl(valueSize)
  ctx.fillStyle = valColor
  ctx.textAlign = align
  ctx.textBaseline = 'middle'
  ctx.fillText(valueText, ax, captionH + (rect.h - captionH - unitH) / 2)

  // ─── Unit — снизу ─────────────────────────────────────────────────────────
  if (hasUnit) {
    ctx.font = subFontTpl(subSize)
    ctx.fillStyle = theme.muted
    ctx.textAlign = align
    ctx.textBaseline = 'bottom'
    ctx.fillText(unit, ax, rect.h)
  }

  ctx.restore()
}
