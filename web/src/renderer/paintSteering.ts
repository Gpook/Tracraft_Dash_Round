/**
 * Steering — положение руля.
 *
 * Горизонтальная полоса, растущая ИЗ ЦЕНТРА в сторону поворота:
 * руль вправо → полоса вправо, руль влево → влево. Центр отмечен риской,
 * поэтому нулевое положение читается мгновенно, без чтения цифр.
 *
 * Сигнал (steer.pos): 0 = полностью влево, 0.5 = центр, 1 = полностью вправо.
 */

import { SteeringWidget } from '@/schema/layout'

const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v))

export function paintSteering(
  ctx: CanvasRenderingContext2D,
  w: SteeringWidget,
  value: number,
  theme: { fg: string; muted: string },
) {
  const { rect, props } = w
  const {
    color,
    trackColor = '#1C1C1E',
    radius = 4,
    centerMark = true,
    showValue = false,
    maxAngle = 450,
    deadzone = 0,
  } = props

  // −1 (влево) … 0 (центр) … +1 (вправо)
  let n = clamp((value - 0.5) * 2, -1, 1)

  // Зона нечувствительности: гасим дребезг около центра, но сохраняем
  // полный размах — остаток диапазона растягивается обратно на 0..1
  if (deadzone > 0 && deadzone < 1) {
    const mag = Math.abs(n)
    n = mag <= deadzone ? 0 : Math.sign(n) * ((mag - deadzone) / (1 - deadzone))
  }

  ctx.save()
  ctx.translate(rect.x, rect.y)

  // ─── Раскладка: полоса сверху, цифры под ней ──────────────────────────────
  const textH = showValue ? Math.min(16, rect.h * 0.34) : 0
  const barH  = Math.max(2, rect.h - textH)
  const halfW = rect.w / 2
  const r     = Math.min(radius, barH / 2)

  // ─── Дорожка ──────────────────────────────────────────────────────────────
  ctx.fillStyle = trackColor
  ctx.beginPath()
  ctx.roundRect(0, 0, rect.w, barH, r)
  ctx.fill()

  // ─── Заполнение от центра ─────────────────────────────────────────────────
  const len = Math.abs(n) * halfW
  if (len > 0.5) {
    const x = n >= 0 ? halfW : halfW - len
    ctx.fillStyle = (color ?? theme.fg) as string
    ctx.beginPath()
    ctx.roundRect(x, 0, len, barH, Math.min(r, len / 2))
    ctx.fill()
  }

  // ─── Риска центра ─────────────────────────────────────────────────────────
  // Рисуется поверх заполнения, чтобы ноль был виден при любом отклонении
  if (centerMark) {
    const markW = 2
    ctx.fillStyle = 'rgba(255,255,255,0.55)'
    ctx.fillRect(halfW - markW / 2, 0, markW, barH)
  }

  // ─── Цифровое значение (градусы) ──────────────────────────────────────────
  if (showValue) {
    const deg = Math.round(n * maxAngle)
    const txt = deg === 0 ? '0°' : `${deg > 0 ? '+' : '−'}${Math.abs(deg)}°`
    ctx.font = `${Math.max(9, Math.round(textH * 0.82))}px Inter, sans-serif`
    ctx.fillStyle = theme.muted
    ctx.textAlign = 'center'
    ctx.textBaseline = 'bottom'
    ctx.fillText(txt, halfW, rect.h)
  }

  ctx.restore()
}
