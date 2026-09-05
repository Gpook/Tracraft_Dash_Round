/**
 * DisplayCanvas — главная канва редактора (466×466, CO5300 AMOLED 1.75")
 *
 * Возможности:
 *  - Круглый clip + safe-area ring
 *  - requestAnimationFrame @ 60 fps — цикл НИКОГДА не перезапускается (refs)
 *  - Клик для выбора виджета
 *  - Delete/Backspace — удаление выбранного виджета
 *  - Drag для перемещения
 *  - 8 resize-ручек (углы + середины рёбер)
 *  - Snap к сетке (SNAP px) — отключается зажатым Alt
 */

import React, { useRef, useEffect, useCallback } from 'react'
import { useEditorStore, useActiveScreen } from '@/store'
import { paintWidget } from '@/renderer/paintWidget'
import { setDeviceMode } from '@/renderer/deviceFont'
import { subscribeSignals, SignalValues } from '@/mock/signalGenerator'
import { Widget, Display, Theme } from '@/schema/layout'
import { drawOrbitIndicator } from '@/renderer/burnInOverlay'

const SNAP = 4
const HANDLE_SIZE = 8
const MIN_W = 20
const MIN_H = 12

type HandleDir = 'nw'|'n'|'ne'|'e'|'se'|'s'|'sw'|'w'

interface DragState {
  kind: 'move' | 'resize'
  widgetId: string
  startMouseX: number; startMouseY: number
  origRect: { x: number; y: number; w: number; h: number }
  dir?: HandleDir
  noSnap?: boolean
}

function snap(v: number, noSnap: boolean) { return noSnap ? Math.round(v) : Math.round(v / SNAP) * SNAP }

function getHandles(r: { x: number; y: number; w: number; h: number }): Record<HandleDir, [number, number]> {
  const { x, y, w, h } = r
  return {
    nw: [x, y], n: [x + w / 2, y], ne: [x + w, y],
    e: [x + w, y + h / 2],
    se: [x + w, y + h], s: [x + w / 2, y + h], sw: [x, y + h],
    w: [x, y + h / 2],
  }
}

function hitHandle(hx: number, hy: number, mx: number, my: number): boolean {
  const hs = HANDLE_SIZE / 2 + 2
  return Math.abs(mx - hx) <= hs && Math.abs(my - hy) <= hs
}

function hitWidget(w: Widget, x: number, y: number): boolean {
  const pad = 4
  return x >= w.rect.x - pad && x <= w.rect.x + w.rect.w + pad
      && y >= w.rect.y - pad && y <= w.rect.y + w.rect.h + pad
}

function resizeCursor(dir: HandleDir): string {
  return { nw: 'nw-resize', n: 'n-resize', ne: 'ne-resize', e: 'e-resize',
           se: 'se-resize', s: 's-resize', sw: 'sw-resize', w: 'w-resize' }[dir]
}

interface Props {
  display: Display
  zoom?: number
  showBurnInOrbit?: boolean
}

// ─── Мутабельная сцена (читается из RAF без перезапуска цикла) ────────────────
interface SceneSnapshot {
  screen: ReturnType<typeof useActiveScreen>
  theme: Theme
  display: Display
  selectedId: string | null
  playing: boolean
  showOrbit: boolean
  deviceMode: boolean
}

export function DisplayCanvas({ display, zoom = 0.82, showBurnInOrbit = false }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const signalsRef = useRef<SignalValues>({})
  const rafRef = useRef<number>(0)
  const dragRef = useRef<DragState | null>(null)
  const sceneRef = useRef<SceneSnapshot>({
    screen: undefined as any,
    theme: { bg: '#000', fg: '#fff', accent: '#f00', muted: '#666', warn: '#ff0', crit: '#f00' },
    display,
    selectedId: null,
    playing: true,
    showOrbit: false,
    deviceMode: false,
  })

  const { layout, activeScreenIdx, selectedWidgetId, playing, deviceMode } = useEditorStore()
  const screen = useActiveScreen()
  const { selectWidget, updateWidget, removeWidget } = useEditorStore()

  // ── Синхронизация сцены в ref (без перезапуска RAF) ──────────────────────
  useEffect(() => {
    sceneRef.current = {
      screen, theme: layout.theme, display,
      selectedId: selectedWidgetId, playing, showOrbit: showBurnInOrbit,
      deviceMode,
    }
  })

  // ── Mock-сигналы ─────────────────────────────────────────────────────────
  useEffect(() => {
    // Сигналы идут всегда; когда paused — просто не обновляем ref
    return subscribeSignals(v => {
      if (sceneRef.current.playing) signalsRef.current = v
    })
  }, []) // подписываемся один раз на весь lifecycle компонента

  // ── RAF-цикл: запускается один раз, никогда не перезапускается ────────────
  useEffect(() => {
    const canvas = canvasRef.current; if (!canvas) return
    const ctx = canvas.getContext('2d')!

    const frame = () => {
      const { screen: s, theme, display: disp, selectedId, playing: pl, showOrbit,
              deviceMode: dev } = sceneRef.current

      // Флаг ставится один раз на кадр: painter-функции читают его из модуля,
      // чтобы не прокидывать параметр через все подписи
      setDeviceMode(dev)

      const t = performance.now() / 1000
      const orbitPhase = showOrbit ? Math.floor(t / 2) % 9 : 0
      // Фон экрана переопределяет фон темы, если задан
      const bg = s?.bg ?? theme.bg
      drawFrame(ctx, disp, theme, bg, s?.widgets ?? [], signalsRef.current, selectedId, t, pl, orbitPhase, showOrbit)
      rafRef.current = requestAnimationFrame(frame)
    }
    rafRef.current = requestAnimationFrame(frame)
    return () => cancelAnimationFrame(rafRef.current)
  }, []) // пустые зависимости — цикл стабилен навсегда

  // ── Delete / Backspace — удаление выбранного виджета ─────────────────────
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      // Игнорировать если фокус в input/select (переименование экрана, панель свойств)
      const tag = (e.target as HTMLElement).tagName
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return
      if (e.key === 'Delete' || e.key === 'Backspace') {
        const { selectedWidgetId: id, activeScreenIdx: si } = useEditorStore.getState()
        if (id) { e.preventDefault(); removeWidget(si, id) }
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [removeWidget])

  // ── Координаты канвы из события ──────────────────────────────────────────
  const toCanvas = useCallback((e: React.MouseEvent) => {
    const canvas = canvasRef.current!
    const cr = canvas.getBoundingClientRect()
    const sy = canvas.height / cr.height
    return { x: (e.clientX - cr.left) * sy, y: (e.clientY - cr.top) * sy }
  }, [])

  // ── MouseDown ─────────────────────────────────────────────────────────────
  const onMouseDown = useCallback((e: React.MouseEvent) => {
    const { x, y } = toCanvas(e)
    const noSnap = e.altKey
    const widgets = [...(screen?.widgets ?? [])].sort((a, b) => (b.z ?? 0) - (a.z ?? 0))

    // Ручки выбранного виджета
    if (selectedWidgetId) {
      const sel = screen?.widgets.find(w => w.id === selectedWidgetId)
      if (sel) {
        const handles = getHandles(sel.rect)
        for (const [dir, [hx, hy]] of Object.entries(handles) as [HandleDir, [number, number]][]) {
          if (hitHandle(hx, hy, x, y)) {
            dragRef.current = { kind: 'resize', widgetId: sel.id, dir, startMouseX: x, startMouseY: y, origRect: { ...sel.rect }, noSnap }
            return
          }
        }
      }
    }

    const hit = widgets.find(w => hitWidget(w, x, y))
    selectWidget(hit?.id ?? null)
    if (hit) {
      dragRef.current = { kind: 'move', widgetId: hit.id, startMouseX: x, startMouseY: y, origRect: { ...hit.rect }, noSnap }
    } else {
      dragRef.current = null
    }
  }, [screen, selectedWidgetId, selectWidget, toCanvas])

  // ── MouseMove ─────────────────────────────────────────────────────────────
  const onMouseMove = useCallback((e: React.MouseEvent) => {
    const { x, y } = toCanvas(e)
    const canvas = canvasRef.current!
    const drag = dragRef.current

    if (!drag && selectedWidgetId) {
      const sel = screen?.widgets.find(w => w.id === selectedWidgetId)
      if (sel) {
        const handles = getHandles(sel.rect)
        for (const [dir, [hx, hy]] of Object.entries(handles) as [HandleDir, [number, number]][]) {
          if (hitHandle(hx, hy, x, y)) { canvas.style.cursor = resizeCursor(dir); return }
        }
      }
    }

    if (!drag) {
      const hit = [...(screen?.widgets ?? [])].sort((a, b) => (b.z ?? 0) - (a.z ?? 0)).find(w => hitWidget(w, x, y))
      canvas.style.cursor = hit ? 'grab' : 'default'; return
    }

    const dx = x - drag.startMouseX, dy = y - drag.startMouseY
    const ns = drag.noSnap || e.altKey

    if (drag.kind === 'move') {
      applyRect(drag.widgetId, { ...drag.origRect, x: snap(drag.origRect.x + dx, ns), y: snap(drag.origRect.y + dy, ns) })
    } else if (drag.kind === 'resize' && drag.dir) {
      applyRect(drag.widgetId, applyResize(drag.origRect, drag.dir, dx, dy, ns))
    }
    canvas.style.cursor = drag.kind === 'move' ? 'grabbing' : drag.dir ? resizeCursor(drag.dir) : 'default'
  }, [screen, selectedWidgetId, toCanvas])

  function applyRect(widgetId: string, newRect: { x: number; y: number; w: number; h: number }) {
    const w = screen?.widgets.find(w => w.id === widgetId)
    if (!w) return
    updateWidget(activeScreenIdx, { ...w, rect: { ...w.rect, ...newRect } })
  }

  const onMouseUp = useCallback(() => { dragRef.current = null }, [])

  const W = display.w, H = display.h
  return (
    <canvas
      ref={canvasRef}
      width={W} height={H}
      style={{
        width: W * zoom, height: H * zoom,
        borderRadius: display.shape === 'round' ? '50%' : 0,
        userSelect: 'none',
        boxShadow: '0 0 0 1px rgba(255,255,255,0.06), 0 8px 48px rgba(0,0,0,0.8)',
      }}
      onMouseDown={onMouseDown}
      onMouseMove={onMouseMove}
      onMouseUp={onMouseUp}
      onMouseLeave={onMouseUp}
    />
  )
}

// ─── Resize-математика ────────────────────────────────────────────────────────

function applyResize(
  orig: { x: number; y: number; w: number; h: number },
  dir: HandleDir, dx: number, dy: number, noSnap: boolean
) {
  let { x, y, w, h } = orig
  const sn = (v: number) => snap(v, noSnap)
  if (dir.includes('e')) { w = Math.max(MIN_W, sn(orig.w + dx)) }
  if (dir.includes('w')) { const nw = Math.max(MIN_W, sn(orig.w - dx)); x = sn(orig.x + orig.w - nw); w = nw }
  if (dir.includes('s')) { h = Math.max(MIN_H, sn(orig.h + dy)) }
  if (dir.includes('n')) { const nh = Math.max(MIN_H, sn(orig.h - dy)); y = sn(orig.y + orig.h - nh); h = nh }
  return { x, y, w, h }
}

// ─── Рендер одного кадра ─────────────────────────────────────────────────────

function drawFrame(
  ctx: CanvasRenderingContext2D,
  display: Display,
  theme: Theme,
  bg: string,
  widgets: Widget[],
  signals: SignalValues,
  selectedId: string | null,
  time: number,
  playing: boolean,
  orbitPhase: number = 0,
  showOrbit: boolean = false,
) {
  const { w, h, shape, safeInset = 10 } = display
  const cx = w / 2, cy = h / 2, r = w / 2

  ctx.clearRect(0, 0, w, h)

  // Круглый clip
  if (shape === 'round') {
    ctx.save()
    ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.clip()
  }

  // Фон (screen.bg с фолбэком на theme.bg)
  ctx.fillStyle = bg
  ctx.fillRect(0, 0, w, h)

  // Виджеты
  const sorted = [...widgets].sort((a, b) => (a.z ?? 0) - (b.z ?? 0))
  for (const widget of sorted) {
    ctx.save()
    try { paintWidget(ctx, widget, signals, theme, time, display) } catch { /* защита от краша рендерера */ }
    ctx.restore()
  }

  if (shape === 'round') ctx.restore()

  // Safe-area ring
  if (shape === 'round' && safeInset > 0) {
    ctx.save()
    ctx.strokeStyle = 'rgba(255,255,255,0.06)'
    ctx.lineWidth = 1; ctx.setLineDash([4, 8])
    ctx.beginPath(); ctx.arc(cx, cy, r - safeInset, 0, Math.PI * 2); ctx.stroke()
    ctx.setLineDash([]); ctx.restore()
  }

  // Орбита
  if (showOrbit) drawOrbitIndicator(ctx, w, h, orbitPhase)

  // Выделение + ручки
  if (selectedId) {
    const sel = widgets.find(ww => ww.id === selectedId)
    if (sel) drawSelectionOverlay(ctx, sel.rect)
  }

  // Overlay паузы (полупрозрачный, виджеты видны под ним)
  if (!playing) {
    ctx.fillStyle = 'rgba(0,0,0,0.50)'
    ctx.fillRect(0, 0, w, h)
    ctx.font = 'bold 13px Inter,sans-serif'
    ctx.fillStyle = 'rgba(255,255,255,0.5)'
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
    ctx.fillText('▶  Click to resume', cx, cy)
  }
}

function drawSelectionOverlay(
  ctx: CanvasRenderingContext2D,
  rect: { x: number; y: number; w: number; h: number },
) {
  const { x, y, w, h } = rect
  ctx.save()
  ctx.strokeStyle = '#0A84FF'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 3])
  ctx.strokeRect(x - 2, y - 2, w + 4, h + 4)
  ctx.setLineDash([])

  const hs = HANDLE_SIZE
  const handles = getHandles({ x: x - 2, y: y - 2, w: w + 4, h: h + 4 })
  for (const [hx, hy] of Object.values(handles) as [number, number][]) {
    ctx.fillStyle = '#0A84FF'; ctx.strokeStyle = '#FFF'; ctx.lineWidth = 1
    ctx.fillRect(hx - hs / 2, hy - hs / 2, hs, hs)
    ctx.strokeRect(hx - hs / 2, hy - hs / 2, hs, hs)
  }
  ctx.restore()
}
