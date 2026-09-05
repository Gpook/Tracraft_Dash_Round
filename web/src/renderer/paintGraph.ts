/**
 * Graph — линейный график с буфером истории.
 *
 * Для каждого виджета хранится отдельный буфер (circular buffer)
 * объёмом windowSec * 60 точек (≈1800 для 30 с @ 60fps).
 * Поддерживает несколько кривых (props.signals[]).
 */

import { GraphWidget } from '@/schema/layout'
import type { SignalMap } from './paintWidget'

// ─── Буфер истории ────────────────────────────────────────────────────────────

const MAX_PTS = 3600  // 60 с @ 60 fps

interface SignalHistory {
  data: Float32Array
  head: number   // следующая позиция записи
  size: number   // сколько точек заполнено
}

// widgetId → signalId → history
const historyStore = new Map<string, Map<string, SignalHistory>>()

function getHistory(widgetId: string, signalId: string, capacity: number): SignalHistory {
  if (!historyStore.has(widgetId)) historyStore.set(widgetId, new Map())
  const wMap = historyStore.get(widgetId)!
  if (!wMap.has(signalId)) {
    wMap.set(signalId, { data: new Float32Array(capacity), head: 0, size: 0 })
  }
  return wMap.get(signalId)!
}

function push(hist: SignalHistory, value: number) {
  hist.data[hist.head] = value
  hist.head = (hist.head + 1) % hist.data.length
  if (hist.size < hist.data.length) hist.size++
}

function getAt(hist: SignalHistory, i: number): number {
  // i=0 → oldest, i=size-1 → newest
  const idx = (hist.head - hist.size + i + hist.data.length * 2) % hist.data.length
  return hist.data[idx]
}

// ─── Рендерер ─────────────────────────────────────────────────────────────────

export function paintGraph(
  ctx: CanvasRenderingContext2D,
  widget: GraphWidget,
  signals: SignalMap,
  theme: { fg: string; muted: string },
  _time: number,
) {
  const { rect, props, id } = widget
  const { min, max, windowSec = 30, lineWidth = 2, fill = true, signals: sigDefs = [] } = props

  const capacity = Math.min(MAX_PTS, Math.ceil(windowSec * 60))

  // Добавляем текущие значения в буфер
  for (const def of sigDefs) {
    const hist = getHistory(id, def.signal, capacity)
    push(hist, signals[def.signal] ?? 0)
  }

  const { x, y, w, h } = rect
  const PAD_L = 28, PAD_R = 4, PAD_T = 6, PAD_B = 18

  ctx.save()
  ctx.translate(x, y)

  // ── Фон ──────────────────────────────────────────────────────────────────
  ctx.fillStyle = 'rgba(0,0,0,0.45)'
  ctx.beginPath()
  ctx.roundRect(0, 0, w, h, 6)
  ctx.fill()

  const gx = PAD_L, gy = PAD_T
  const gw = w - PAD_L - PAD_R
  const gh = h - PAD_T - PAD_B

  // ── Горизонтальные линии сетки ────────────────────────────────────────────
  ctx.strokeStyle = 'rgba(255,255,255,0.07)'
  ctx.lineWidth = 0.5
  for (let i = 0; i <= 4; i++) {
    const ly = gy + gh * (i / 4)
    ctx.beginPath(); ctx.moveTo(gx, ly); ctx.lineTo(gx + gw, ly); ctx.stroke()

    // Y-метки
    const labelVal = max - (max - min) * (i / 4)
    ctx.font = '8px Inter, sans-serif'
    ctx.fillStyle = theme.muted
    ctx.textAlign = 'right'
    ctx.textBaseline = 'middle'
    ctx.fillText(labelVal.toFixed(0), gx - 3, ly)
  }

  // ── Кривые сигналов ──────────────────────────────────────────────────────
  for (const def of sigDefs) {
    const hist = getHistory(id, def.signal, capacity)
    if (hist.size < 2) continue

    const n = hist.size
    ctx.beginPath()
    let first = true

    for (let i = 0; i < n; i++) {
      const val = getAt(hist, i)
      const px = gx + (i / (n - 1)) * gw
      const py = gy + gh * (1 - Math.max(0, Math.min(1, (val - min) / (max - min))))

      if (first) { ctx.moveTo(px, py); first = false }
      else        { ctx.lineTo(px, py) }
    }

    ctx.strokeStyle = def.color
    ctx.lineWidth = lineWidth
    ctx.lineJoin = 'round'
    ctx.stroke()

    // Залитая область под кривой
    if (fill) {
      ctx.lineTo(gx + gw, gy + gh)
      ctx.lineTo(gx, gy + gh)
      ctx.closePath()
      const grad = ctx.createLinearGradient(0, gy, 0, gy + gh)
      grad.addColorStop(0, def.color + '55')
      grad.addColorStop(1, def.color + '00')
      ctx.fillStyle = grad
      ctx.fill()
    }

    // Последнее значение — точка и цифра
    const lastVal = getAt(hist, n - 1)
    const lastX = gx + gw
    const lastY = gy + gh * (1 - Math.max(0, Math.min(1, (lastVal - min) / (max - min))))
    ctx.fillStyle = def.color
    ctx.beginPath(); ctx.arc(lastX, lastY, 3, 0, Math.PI * 2); ctx.fill()
  }

  // ── Ось X (время) ─────────────────────────────────────────────────────────
  ctx.strokeStyle = 'rgba(255,255,255,0.15)'
  ctx.lineWidth = 0.5
  ctx.beginPath(); ctx.moveTo(gx, gy + gh); ctx.lineTo(gx + gw, gy + gh); ctx.stroke()

  // Подпись сигналов
  let legendX = gx
  for (const def of sigDefs) {
    const shortName = def.signal.split('.').pop() ?? def.signal
    ctx.font = 'bold 8px Inter, sans-serif'
    ctx.fillStyle = def.color
    ctx.textAlign = 'left'
    ctx.textBaseline = 'bottom'
    ctx.fillText(shortName, legendX, h - 2)
    legendX += ctx.measureText(shortName).width + 8
  }

  ctx.restore()
}
