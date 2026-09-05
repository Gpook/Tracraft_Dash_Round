/**
 * Warning overlay — появляется когда сигнал выходит за допустимый порог.
 *
 * Поведение:
 *  • Без настроенного условия → всегда скрыт
 *  • При первом срабатывании показывается на 2 секунды (auto-hide)
 *  • Без моргания — только мягкое пульсирование масштаба (≤6%)
 *  • Затемнение 30% + круг с градиентом + иконка + label + значение
 *
 * ВАЖНО про геометрию: rect умышленно НЕ используется для отрисовки.
 * Предупреждение о падении давления масла должно читаться мгновенно и не
 * может зависеть от того, в какой угол его положили в редакторе. Поэтому
 * оверлей всегда занимает весь экран, а круг центрируется по дисплею —
 * ровно так же, как paintWarning() в src/widget_render.cpp. Рамка остаётся
 * только для выбора и перетаскивания виджета в редакторе.
 */

import { WarningWidget } from '@/schema/layout'
import { drawWarnTriangle, fontOf } from './deviceFont'
import type { SignalMap } from './paintWidget'

// Хранит "показывать до" (в секундах t) для каждого виджета по id
// Сбрасывается автоматически после истечения 2-секундного окна
const showUntilMap = new Map<string, number>()

export function paintWarning(
  ctx: CanvasRenderingContext2D,
  w: WarningWidget,
  value: number,
  unit: string,
  _theme: { fg: string; muted: string },
  time: number,
  signals: SignalMap,
  display: { w: number; h: number },
) {
  const { props } = w
  const {
    color   = '#FF3B30',
    label,
    triggerBelow,
    triggerAbove,
  } = props

  // Условие: либо пороги из панели свойств, либо when{} из схемы лейаута.
  // when{} поддержан потому, что готовые лейауты (mx5-track) описывают
  // аварии именно им, и без этого редактор не показывал их вовсе, а
  // прошивка показывала — расхождение, которое сбивало с толку.
  const when = (props as any).when as
    | { signal: string; cmp?: '<' | '>'; value?: number }
    | undefined

  let triggered = false
  let shown = value

  if (when?.signal) {
    const v = signals[when.signal] ?? 0
    const ref = when.value ?? 0
    triggered = when.cmp === '>' ? v > ref : v < ref
    shown = v
  } else if (triggerBelow !== undefined || triggerAbove !== undefined) {
    if (triggerBelow !== undefined) triggered = triggered || value < triggerBelow
    if (triggerAbove !== undefined) triggered = triggered || value > triggerAbove
  } else {
    // Условия нет вообще → виджет не мусорит экран
    return
  }

  // При первом срабатывании — открываем 2-секундное окно показа
  if (triggered && !showUntilMap.has(w.id)) {
    showUntilMap.set(w.id, time + 2.0)
  }

  // Показывать если мы в пределах открытого окна
  const showUntil = showUntilMap.get(w.id)
  const shouldShow = showUntil !== undefined && time <= showUntil

  if (!shouldShow) {
    // Чистим просроченную запись
    if (showUntil !== undefined) showUntilMap.delete(w.id)
    return
  }

  const cx = display.w / 2
  const cy = display.h / 2
  const R  = Math.min(display.w, display.h) * 0.28

  // Мягкое пульсирование масштаба (без on/off моргания)
  const pulseScale = 1 + 0.06 * ((Math.sin(time * Math.PI * 4) + 1) / 2)

  ctx.save()

  // ── 1. Затемнение фона 30% на весь экран ──────────────────────────────────
  ctx.fillStyle = 'rgba(0, 0, 0, 0.30)'
  ctx.fillRect(0, 0, display.w, display.h)

  // ── 2. Красный круг ───────────────────────────────────────────────────────
  ctx.save()
  ctx.translate(cx, cy)
  ctx.scale(pulseScale, pulseScale)

  ctx.shadowColor = color
  ctx.shadowBlur = 22

  const grad = ctx.createRadialGradient(0, 0, R * 0.1, 0, 0, R)
  grad.addColorStop(0, `${color}CC`)
  grad.addColorStop(0.7, `${color}99`)
  grad.addColorStop(1, `${color}33`)
  ctx.fillStyle = grad
  ctx.beginPath()
  ctx.arc(0, 0, R, 0, Math.PI * 2)
  ctx.fill()
  ctx.shadowBlur = 0
  ctx.restore()

  // ── 3. Иконка ─────────────────────────────────────────────────────────────
  // Вектор, а не emoji: во встроенных шрифтах прошивки emoji отсутствует,
  // и раньше именно из-за него знак аварии на устройстве выглядел иначе.
  const iconY = label ? cy - R * 0.22 : cy - R * 0.05
  drawWarnTriangle(ctx, cx, iconY, R * 0.62, '#FFFFFF')

  // ── 4. Label ──────────────────────────────────────────────────────────────
  if (label) {
    ctx.font = fontOf(Math.max(8, R * 0.28), { bold: true })
    ctx.fillStyle = '#FFFFFF'
    ctx.textAlign = 'center'
    ctx.textBaseline = 'top'
    ctx.fillText(label.toUpperCase(), cx, cy + R * 0.20)
  }

  // ── 5. Числовое значение сигнала ─────────────────────────────────────────
  ctx.font = fontOf(Math.max(7, R * 0.22), { mono: true })
  ctx.fillStyle = 'rgba(255,255,255,0.85)'
  ctx.textAlign = 'center'
  ctx.textBaseline = 'top'
  const valY = cy + R * (label ? 0.50 : 0.32)
  const valText = isNaN(shown)
    ? '–'
    : shown.toFixed(unit === 'bar' ? 2 : shown > 100 ? 0 : 1) + (unit ? ` ${unit}` : '')
  ctx.fillText(valText, cx, valY)

  ctx.restore()
}
