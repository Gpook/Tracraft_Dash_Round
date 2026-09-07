/**
 * Shift Light — три режима:
 *
 * segments  Горизонтальный ряд прямоугольных сегментов.
 *
 * arc       Точки по эллиптической дуге, вписанной в rect.
 *           Количество точек — авто (из длины дуги и размера точки).
 *           При dotSize → больше: меньше точек. При меньше: больше точек.
 *           Все точки мигают синхронно при последней стадии.
 *
 * flash     Заливает весь rect оранжевым → красным (70% opacity).
 *           Ставь rect = {0,0,466,466} для покрытия всего экрана.
 */

import { ShiftLightWidget } from '@/schema/layout'

// Оценка длины параметрической кривой (для расчёта кол-ва точек)
function ellipseArcLength(a: number, b: number, t1: number, t2: number, steps = 64): number {
  let len = 0
  for (let i = 0; i < steps; i++) {
    const ta = t1 + (t2 - t1) * i / steps
    const tb = t1 + (t2 - t1) * (i + 1) / steps
    const dx = a * (Math.cos(tb) - Math.cos(ta))
    const dy = b * (Math.sin(tb) - Math.sin(ta))
    len += Math.sqrt(dx * dx + dy * dy)
  }
  return len
}

export function paintShiftLight(
  ctx: CanvasRenderingContext2D,
  w: ShiftLightWidget,
  value: number,
  _theme: { fg: string; muted: string },
  time: number,
) {
  const { rect, props } = w
  const { mode = 'segments', stages, dotSize = 0 } = props
  if (stages.length === 0) return

  // Текущая активная стадия
  let stageCount = 0
  for (let i = 0; i < stages.length; i++) {
    if (value >= stages[i].at) stageCount = i + 1
  }
  const allLit = stageCount >= stages.length
  const lastStage = stages[stages.length - 1]
  const blinkHz = lastStage.blinkHz ?? 6
  const blinkOn = Math.sin(time * blinkHz * Math.PI * 2) > 0

  ctx.save()

  // ── SEGMENTS ──────────────────────────────────────────────────────────────
  if (mode === 'segments') {
    ctx.translate(rect.x, rect.y)
    const n = stages.length
    const gap = 3
    const segW = (rect.w - gap * (n - 1)) / n

    for (let i = 0; i < n; i++) {
      const lit = i < stageCount
      let alpha = lit ? 1 : 0.10
      if (allLit) alpha = blinkOn ? 1 : 0.08
      else if (lit && i === n - 1 && lastStage.blinkHz) alpha = blinkOn ? 1 : 0.08

      ctx.globalAlpha = alpha
      ctx.fillStyle = stages[i].color
      ctx.beginPath()
      ctx.roundRect(i * (segW + gap), 0, segW, rect.h, 3)
      ctx.fill()
    }
    ctx.globalAlpha = 1
  }

  // ── ARC ───────────────────────────────────────────────────────────────────
  else if (mode === 'arc') {
    // Эллиптическая дуга вписывается в rect.
    // Центр эллипса — у нижнего края; дуга проходит по верхней части.
    const PAD = 2
    const a = (rect.w / 2 - PAD) * 0.97   // полуось X
    const b = Math.max(rect.h, 8) * 0.92   // полуось Y
    const ecx = rect.x + rect.w / 2
    const ecy = rect.y + rect.h             // центр эллипса — нижний край rect

    const startAngle = -170 * Math.PI / 180   // левый конец дуги
    const endAngle   =  -10 * Math.PI / 180   // правый конец дуги

    // Размер точки: dotSize prop или авто по высоте rect
    const autoR = Math.max(3, Math.min(rect.h * 0.40, 12))
    const dotR = dotSize > 0 ? dotSize : autoR

    // Количество точек = длина дуги / (диаметр + зазор)
    const arcLen = ellipseArcLength(a, b, startAngle, endAngle)
    const gap = dotR * 0.6
    const dotCount = Math.max(stages.length, Math.min(30, Math.floor(arcLen / (dotR * 2 + gap))))

    // Сколько точек "активно"
    // Линейное отображение: от stages[0].at до stages[last].at → 0 → dotCount
    const vMin = stages[0].at
    const vMax = lastStage.at
    const fraction = Math.max(0, Math.min(1, (value - vMin) / (vMax - vMin + 1)))
    const litCount = Math.round(fraction * dotCount)

    for (let i = 0; i < dotCount; i++) {
      const t = i / (dotCount - 1)
      const angle = startAngle + t * (endAngle - startAngle)
      const px = ecx + a * Math.cos(angle)
      const py = ecy + b * Math.sin(angle)

      const lit = i < litCount
      // Цвет точки: из какой стадии она
      const stageIdx = Math.min(stages.length - 1, Math.floor(i * stages.length / dotCount))
      const dotColor = stages[stageIdx].color

      let alpha: number
      if (allLit) {
        // Мигание всех точек разом: яркая фаза — полный цвет + glow,
        // тёмная фаза — ~22 % цвета (точки видны, но явно погашены).
        if (blinkOn) {
          alpha = 1.0
          ctx.shadowColor = dotColor; ctx.shadowBlur = 10
        } else {
          alpha = 0.22
          ctx.shadowBlur = 0
        }
      } else if (lit) {
        alpha = 1
        ctx.shadowColor = dotColor; ctx.shadowBlur = 6
      } else {
        alpha = 0.10
        ctx.shadowBlur = 0
      }

      ctx.globalAlpha = alpha
      ctx.fillStyle = lit || allLit ? dotColor : '#2C2C2E'
      ctx.beginPath()
      ctx.arc(px, py, dotR, 0, Math.PI * 2)
      ctx.fill()
    }
    ctx.globalAlpha = 1
    ctx.shadowBlur = 0
  }

  // ── FLASH ─────────────────────────────────────────────────────────────────
  else if (mode === 'flash') {
    if (stageCount === 0) { ctx.restore(); return }

    // Пульсация: оранжевый (255,140,0) → красный (255,30,20)
    const pulse = Math.sin(time * blinkHz * Math.PI * 2)
    const gComp = Math.round(lerp(20, 140, (pulse + 1) / 2))
    ctx.fillStyle = `rgba(255, ${gComp}, 10, 0.70)`
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h)

    // Дополнительный красный всплеск в пике
    if (pulse < -0.6) {
      const extra = (-pulse - 0.6) / 0.4
      ctx.fillStyle = `rgba(255, 20, 20, ${0.35 * extra})`
      ctx.fillRect(rect.x, rect.y, rect.w, rect.h)
    }
  }

  ctx.restore()
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}
