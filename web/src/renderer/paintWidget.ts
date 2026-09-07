/**
 * Диспетчер: вызывает нужный paint-модуль по типу виджета.
 */

import { Widget, Theme, SignalId } from '@/schema/layout'
import { paintNumeric }   from './paintNumeric'
import { paintLabel }     from './paintLabel'
import { paintSteering }  from './paintSteering'
import { paintTireTemp }  from './paintTireTemp'
import { paintShiftLight } from './paintShiftLight'
import { paintGForce }    from './paintGForce'
import { paintWarning }   from './paintWarning'
import { paintGraph }     from './paintGraph'
import { fontOf }         from './deviceFont'

export type SignalMap = Record<SignalId, number>

const themeMin = (t: Theme) => ({ fg: t.fg, muted: t.muted })

export function paintWidget(
  ctx: CanvasRenderingContext2D,
  widget: Widget,
  signals: SignalMap,
  theme: Theme,
  time: number,
  display: { w: number; h: number },
) {
  const value = widget.signal ? (signals[widget.signal] ?? 0) : 0
  const unit  = widget.unit ?? ''
  const th    = themeMin(theme)

  switch (widget.type) {

    case 'numeric':
      paintNumeric(ctx, widget, value, unit, th, time)
      break

    case 'label':
      paintLabel(ctx, widget, th)
      break

    case 'shift_light':
      paintShiftLight(ctx, widget, value, th, time)
      break

    case 'bar': {
      const { rect, props } = widget
      const n01 = Math.max(0, Math.min(1, (value - props.min) / (props.max - props.min)))
      const r   = props.radius ?? 4
      ctx.save()
      ctx.translate(rect.x, rect.y)
      // Track
      ctx.fillStyle = props.trackColor ?? '#1C1C1E'
      ctx.beginPath(); ctx.roundRect(0, 0, rect.w, rect.h, r); ctx.fill()
      // Fill
      ctx.fillStyle = props.color ?? theme.fg
      ctx.beginPath()
      if (props.orientation === 'vertical') {
        const fillH = rect.h * n01
        ctx.roundRect(0, rect.h - fillH, rect.w, Math.max(r, fillH), r)
      } else {
        ctx.roundRect(0, 0, Math.max(r, rect.w * n01), rect.h, r)
      }
      ctx.fill()
      ctx.restore()
      break
    }

    case 'warning':
      // display, а не rect: оверлей аварии всегда во весь экран — см. комментарий
      // о геометрии в paintWarning.ts
      paintWarning(ctx, widget, value, unit, th, time, signals, display)
      break

    case 'graph':
      paintGraph(ctx, widget, signals, th, time)
      break

    case 'gforce':
      paintGForce(ctx, widget, signals, th, time)
      break

    case 'steering': {
      // Отсутствующий сигнал = руль по центру. Общий fallback в 0 здесь не
      // подходит: 0 — это законное «полностью влево», и полоса без данных
      // упиралась бы в край, изображая несуществующий поворот.
      const pos = widget.signal ? (signals[widget.signal] ?? 0.5) : 0.5
      paintSteering(ctx, widget, pos, th)
      break
    }

    case 'tire_temp':
      // Сигналы не берутся из widget.signal: их двадцать, и виджет собирает
      // имена сам из префикса — см. paintTireTemp.
      paintTireTemp(ctx, widget, signals, th)
      break

    case 'clock': {
      const { rect, props } = widget
      const now = new Date()
      const h = now.getHours(), m = now.getMinutes(), s = now.getSeconds()
      const hh = props.format === '12h' ? (h % 12 || 12) : h
      const mm = String(m).padStart(2, '0')
      const ss = String(s).padStart(2, '0')
      const txt = props.showSeconds ? `${hh}:${mm}:${ss}` : `${hh}:${mm}`
      ctx.save()
      ctx.translate(rect.x + rect.w / 2, rect.y + rect.h / 2)
      ctx.font = fontOf(Math.min(rect.h * 0.65, 48), { bold: true, mono: true })
      ctx.fillStyle = props.color ?? theme.fg
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
      ctx.fillText(txt, 0, 0)
      ctx.restore()
      break
    }

    case 'lap_timer':
      paintPlaceholder(ctx, widget.rect, 'Lap Timer', theme)
      break

    case 'needle_gauge':
    case 'image':
      paintPlaceholder(ctx, widget.rect, widget.type, theme)
      break

    default:
      paintPlaceholder(ctx, (widget as any).rect, (widget as any).type ?? '?', theme)
  }
}

function paintPlaceholder(
  ctx: CanvasRenderingContext2D,
  rect: { x: number; y: number; w: number; h: number },
  label: string,
  theme: Theme,
) {
  ctx.save()
  ctx.strokeStyle = theme.muted; ctx.lineWidth = 1; ctx.setLineDash([4, 4])
  ctx.strokeRect(rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1)
  ctx.setLineDash([])
  ctx.font = '10px Inter, sans-serif'
  ctx.fillStyle = theme.muted
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
  ctx.fillText(label, rect.x + rect.w / 2, rect.y + rect.h / 2)
  ctx.restore()
}
