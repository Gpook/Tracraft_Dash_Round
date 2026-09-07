/**
 * Label — статический текст.
 *
 * Размер шрифта:
 *   props.fontSize > 0  → фиксированный размер
 *   иначе               → автоподгонка под rect (по высоте И ширине)
 */

import { LabelWidget } from '@/schema/layout'
import { fitFontSize } from './fitText'
import { fontOf } from './deviceFont'

// Текст занимает не всю высоту рамки — иначе выносные элементы букв режутся
const HEIGHT_RATIO = 0.72
const WIDTH_RATIO  = 0.94

export function paintLabel(
  ctx: CanvasRenderingContext2D,
  w: LabelWidget,
  theme: { fg: string },
) {
  const { rect, props } = w
  const { text, align = 'center', color, fontSize } = props

  const fontTpl = (px: number) => fontOf(px)

  const size = fontSize && fontSize > 0
    ? fontSize
    : fitFontSize(ctx, text, rect.w * WIDTH_RATIO, rect.h * HEIGHT_RATIO, fontTpl)

  ctx.save()
  ctx.translate(rect.x, rect.y)
  ctx.font = fontTpl(size)
  ctx.fillStyle = (color ?? theme.fg) as string
  ctx.textAlign = align
  ctx.textBaseline = 'middle'
  const ax = align === 'center' ? rect.w / 2 : align === 'right' ? rect.w : 0
  ctx.fillText(text, ax, rect.h / 2)
  ctx.restore()
}
