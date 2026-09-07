/**
 * Warning overlay — появляется когда сигнал выходит за допустимый порог.
 *
 * Поведение:
 *  • Без настроенного условия → всегда скрыт
 *  • При первом срабатывании показывается на 2 секунды (auto-hide)
 *  • Плоская заливка экрана цветом аварии на 70% + подпись и значение
 *
 * Раньше здесь были затемнение фона, радиальный градиент, векторная иконка и
 * пульсация масштаба. На устройстве это стоило двух полноэкранных проходов по
 * PSRAM и 130 концентрических окружностей на кадр — рендер проваливался до
 * 90-110 мс, то есть 8 FPS, ровно в момент аварии. Плоская заливка делает то
 * же самое одним проходом.
 *
 * ВАЖНО про геометрию: rect умышленно НЕ используется для отрисовки.
 * Предупреждение о падении давления масла должно читаться мгновенно и не
 * может зависеть от того, в какой угол его положили в редакторе. Поэтому
 * оверлей всегда занимает весь экран — ровно так же, как paintWarning() в
 * src/widget_render.cpp. Рамка остаётся только для выбора и перетаскивания
 * виджета в редакторе.
 */

import { WarningWidget } from '@/schema/layout'
import { fontOf } from './deviceFont'
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

  ctx.save()

  // ── Заливка всего экрана цветом аварии на 70% ─────────────────────────────
  ctx.globalAlpha = 0.70
  ctx.fillStyle = color
  ctx.fillRect(0, 0, display.w, display.h)
  ctx.globalAlpha = 1

  // ── Подпись и значение по центру ──────────────────────────────────────────
  // Кегли — доли ширины панели, чтобы текст читался с водительского места
  // без настройки. Те же коэффициенты, что в прошивке.
  const labelPx = display.w * 0.13
  const valuePx = display.w * 0.10

  const valText = isNaN(shown)
    ? '–'
    : shown.toFixed(unit === 'bar' ? 2 : shown > 100 ? 0 : 1) + (unit ? ` ${unit}` : '')

  ctx.fillStyle = '#FFFFFF'
  ctx.textAlign = 'center'
  ctx.textBaseline = 'top'

  if (label) {
    ctx.font = fontOf(labelPx, { bold: true })
    ctx.fillText(label.toUpperCase(), cx, cy - labelPx)

    ctx.font = fontOf(valuePx, { mono: true })
    ctx.fillText(valText, cx, cy + labelPx / 5)
  } else {
    ctx.font = fontOf(valuePx, { mono: true })
    ctx.fillText(valText, cx, cy - valuePx / 2)
  }

  ctx.restore()
}
