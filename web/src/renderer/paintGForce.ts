/**
 * G-Force радар — круговая шкала с движущимся шариком.
 *
 * Макет:
 *   ┌────────────────────────┐
 *   │  ┌────────────────┐    │
 *   │  │   (радар)      │    │  ← квадратная область = min(w,h)*0.82
 *   │  │   •   ← шарик │    │
 *   │  └────────────────┘    │
 *   │     1.23 G  (цифра)    │  ← нижняя 18% высоты
 *   └────────────────────────┘
 *
 * Сигналы:
 *   props.signalX (или 'imu.ax') — поперечная G (+ = вправо)
 *   props.signalY (или 'imu.ay') — продольная G  (+ = вперёд/разгон)
 */

import { GForceWidget } from '@/schema/layout'
import type { SignalMap } from './paintWidget'

export function paintGForce(
  ctx: CanvasRenderingContext2D,
  widget: GForceWidget,
  signals: SignalMap,
  theme: { fg: string; muted: string },
  _time: number,
) {
  const { rect, props } = widget
  const range = props.range ?? 2.0
  const rings = props.rings ?? 2

  const signalX = props.signalX ?? 'imu.ax'
  const signalY = props.signalY ?? 'imu.ay'

  const ax = signals[signalX] ?? 0   // +right, -left
  const ay = signals[signalY] ?? 0   // +accel, -brake

  // ── Геометрия ──────────────────────────────────────────────────────────────
  const { x: rx, y: ry, w: rw, h: rh } = rect

  // Радар занимает верхние ~82% высоты
  const radarSize = Math.min(rw, rh * 0.80)
  const radarX = rx + rw / 2
  const radarY = ry + radarSize / 2 + (rh * 0.04)
  const R = radarSize / 2

  ctx.save()

  // ── Фон радара (тёмный круг) ───────────────────────────────────────────────
  ctx.beginPath()
  ctx.arc(radarX, radarY, R, 0, Math.PI * 2)
  ctx.fillStyle = 'rgba(255,255,255,0.04)'
  ctx.fill()
  ctx.strokeStyle = 'rgba(255,255,255,0.15)'
  ctx.lineWidth = 1
  ctx.stroke()

  // ── Кольца ────────────────────────────────────────────────────────────────
  for (let i = 1; i <= rings; i++) {
    const ringR = R * (i / rings)
    ctx.beginPath()
    ctx.arc(radarX, radarY, ringR, 0, Math.PI * 2)
    ctx.strokeStyle = 'rgba(255,255,255,0.10)'
    ctx.lineWidth = 0.75
    ctx.stroke()

    // Метка G значения
    const gLabel = ((range * i) / rings).toFixed(1)
    ctx.font = `${Math.max(8, R * 0.11)}px Inter, sans-serif`
    ctx.fillStyle = 'rgba(255,255,255,0.25)'
    ctx.textAlign = 'center'
    ctx.textBaseline = 'middle'
    ctx.fillText(gLabel, radarX + ringR * 0.72, radarY - ringR * 0.72)
  }

  // ── Крестовина ────────────────────────────────────────────────────────────
  ctx.strokeStyle = 'rgba(255,255,255,0.12)'
  ctx.lineWidth = 0.75
  ctx.setLineDash([3, 5])
  ctx.beginPath(); ctx.moveTo(radarX - R, radarY); ctx.lineTo(radarX + R, radarY); ctx.stroke()
  ctx.beginPath(); ctx.moveTo(radarX, radarY - R); ctx.lineTo(radarX, radarY + R); ctx.stroke()
  ctx.setLineDash([])

  // Метки осей — соответствуют инверсии: разгон уводит шарик ВНИЗ
  const labelFont = `${Math.max(7, R * 0.10)}px Inter, sans-serif`
  ctx.font = labelFont
  ctx.fillStyle = 'rgba(255,255,255,0.20)'
  ctx.textAlign = 'center'
  ctx.fillText('BRAKE', radarX, radarY - R + 9)
  ctx.fillText('ACCEL', radarX, radarY + R - 5)

  // ── Положение шарика: ИНЕРЦИЯ, а не ускорение ─────────────────────────────
  // Акселерометр даёт ускорение кузова, а водитель/груз ощущает силу в
  // ПРОТИВОПОЛОЖНУЮ сторону — поэтому оба знака инвертированы:
  //   разгон  (ay > 0) → вес назад  → шарик вниз
  //   поворот вправо (ax > 0) → вес влево → шарик влево
  const ballX = radarX - (ax / range) * R
  const ballY = radarY + (ay / range) * R

  // ── Шарик ─────────────────────────────────────────────────────────────────
  const ballR = Math.max(5, R * 0.10)

  // Свечение
  ctx.shadowColor = '#0A84FF'
  ctx.shadowBlur = 14

  const grad = ctx.createRadialGradient(ballX - ballR * 0.3, ballY - ballR * 0.3, 1, ballX, ballY, ballR)
  grad.addColorStop(0, '#60CFFF')
  grad.addColorStop(1, '#005ECC')
  ctx.fillStyle = grad
  ctx.beginPath()
  ctx.arc(ballX, ballY, ballR, 0, Math.PI * 2)
  ctx.fill()
  ctx.shadowBlur = 0

  // ── Цифровое значение внизу ───────────────────────────────────────────────
  const totalG = Math.sqrt(ax * ax + ay * ay)
  const numY = ry + rh - rh * 0.07
  const numFontSize = Math.max(10, rh * 0.13)

  ctx.font = `bold ${numFontSize}px 'JetBrains Mono', monospace`
  ctx.fillStyle = theme.fg
  ctx.textAlign = 'center'
  ctx.textBaseline = 'alphabetic'
  ctx.fillText(totalG.toFixed(2), radarX, numY)

  ctx.font = `${numFontSize * 0.65}px Inter, sans-serif`
  ctx.fillStyle = theme.muted
  ctx.fillText('G', radarX + ctx.measureText(totalG.toFixed(2)).width * 0.5 + numFontSize * 0.4, numY)

  ctx.restore()
}
