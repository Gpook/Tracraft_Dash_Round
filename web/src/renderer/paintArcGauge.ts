/**
 * Arc Gauge — дуговая шкала.
 * Рисуется идентично тому, что делает прошивка с LVGL lv_arc.
 */

import { ArcGaugeWidget, zoneColor, norm, degToRad } from '@/schema/layout'

export function paintArcGauge(
  ctx: CanvasRenderingContext2D,
  w: ArcGaugeWidget,
  value: number,
  theme: { fg: string; muted: string },
  _time: number,
) {
  const { rect, props } = w
  const {
    min,
    max,
    startAngle = 135,
    endAngle = 405,
    thickness = 16,
    rounded = true,
    color,
    trackColor = '#1C1C1E',
    peakHold = false,
    zones,
    ticks,
    showValue = false,
  } = props

  ctx.save()
  ctx.translate(rect.x + rect.w / 2, rect.y + rect.h / 2)

  const r = Math.min(rect.w, rect.h) / 2 - thickness / 2 - 2
  const sweep = endAngle - startAngle
  const valueAngle = startAngle + sweep * norm(value, min, max)

  // ─── Трек (подложка) ──────────────────────────────────────────────────────
  ctx.beginPath()
  ctx.arc(0, 0, r, degToRad(startAngle), degToRad(endAngle))
  ctx.strokeStyle = trackColor
  ctx.lineWidth = thickness
  ctx.lineCap = rounded ? 'round' : 'butt'
  ctx.stroke()

  // ─── Активная дуга ────────────────────────────────────────────────────────
  const activeColor = zoneColor(zones, value, (color ?? theme.fg) as `#${string}`)
  ctx.beginPath()
  ctx.arc(0, 0, r, degToRad(startAngle), degToRad(valueAngle))
  ctx.strokeStyle = activeColor
  ctx.lineWidth = thickness
  ctx.lineCap = rounded ? 'round' : 'butt'
  ctx.stroke()

  // ─── Засечки ──────────────────────────────────────────────────────────────
  if (ticks) {
    paintTicks(ctx, r, ticks, min, max, startAngle, endAngle, thickness, theme.muted)
  }

  // ─── Пик ─────────────────────────────────────────────────────────────────
  if (peakHold) {
    // Пик хранится во внешнем состоянии; здесь рисуем только метку-штрих
    // (реальный пик обновляется store, передаётся как `value` при необходимости)
    // Пропускаем в базовой версии — добавим при интеграции store
  }

  // ─── Значение в центре дуги ──────────────────────────────────────────────
  if (showValue) {
    ctx.font = 'bold 32px Inter, sans-serif'
    ctx.fillStyle = theme.fg
    ctx.textAlign = 'center'
    ctx.textBaseline = 'middle'
    ctx.fillText(Math.round(value).toString(), 0, 0)
  }

  ctx.restore()
}

function paintTicks(
  ctx: CanvasRenderingContext2D,
  r: number,
  ticks: NonNullable<ArcGaugeWidget['props']['ticks']>,
  min: number,
  max: number,
  startAngle: number,
  endAngle: number,
  thickness: number,
  muteColor: string,
) {
  const { major, minor, labels = true, labelDivisor = 1 } = ticks
  const sweep = endAngle - startAngle
  const outerR = r - thickness / 2 - 4
  const majorLen = 12
  const minorLen = 6

  ctx.strokeStyle = muteColor
  ctx.fillStyle = muteColor
  ctx.font = '10px Inter, sans-serif'
  ctx.textAlign = 'center'
  ctx.textBaseline = 'middle'
  ctx.lineWidth = 1.5

  // Major ticks
  const majorStep = major
  for (let v = min; v <= max + 0.001; v += majorStep) {
    const angle = degToRad(startAngle + sweep * norm(v, min, max))
    const cos = Math.cos(angle)
    const sin = Math.sin(angle)
    ctx.beginPath()
    ctx.moveTo(cos * (outerR - majorLen), sin * (outerR - majorLen))
    ctx.lineTo(cos * outerR, sin * outerR)
    ctx.stroke()

    if (labels) {
      const lbl = (v / labelDivisor).toFixed(0)
      ctx.fillText(lbl, cos * (outerR - majorLen - 10), sin * (outerR - majorLen - 10))
    }
  }

  // Minor ticks
  if (minor) {
    ctx.lineWidth = 1
    for (let v = min; v <= max + 0.001; v += minor) {
      if ((v - min) % majorStep === 0) continue
      const angle = degToRad(startAngle + sweep * norm(v, min, max))
      const cos = Math.cos(angle)
      const sin = Math.sin(angle)
      ctx.beginPath()
      ctx.moveTo(cos * (outerR - minorLen), sin * (outerR - minorLen))
      ctx.lineTo(cos * outerR, sin * outerR)
      ctx.stroke()
    }
  }
}
