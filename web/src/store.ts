/**
 * Глобальное состояние редактора — Zustand.
 * Source of truth: Layout JSON. Все изменения идут через action-функции,
 * которые атомарно обновляют нужный кусок и добавляют запись в историю undo.
 */

import { create } from 'zustand'
import {
  Layout, Screen, Widget, Theme, DISPLAY_466, DEFAULT_THEME,
  NumericWidget, ShiftLightWidget, LabelWidget, BarWidget,
} from '@/schema/layout'

// ─── Стартовый лейаут (MX-5 Dash preview) ────────────────────────────────────

const STARTER_LAYOUT: Layout = {
  schema: 1,
  id: 'new-layout',
  name: 'New Layout',
  display: DISPLAY_466,
  theme: DEFAULT_THEME,
  sim: { enabled: true },
  screens: [
    {
      id: 'main',
      name: 'Main',
      widgets: [
        {
          id: 'rpm',
          type: 'numeric',
          rect: { x: 133, y: 86, w: 200, h: 60 },
          z: 1,
          signal: 'engine.rpm',
          unit: 'rpm',
          props: {
            decimals: 0, align: 'center', color: '#FFFFFF',
            showUnit: true, fontSize: 0,
            colorFromZones: true,
            zones: [
              { from: 6600, to: 7200, color: '#FFCC00' },
              { from: 7200, to: 8000, color: '#FF3B30' },
            ],
          },
        } satisfies NumericWidget,

        {
          id: 'shift',
          type: 'shift_light',
          rect: { x: 103, y: 40, w: 260, h: 26 },
          z: 1,
          signal: 'engine.rpm',
          props: {
            mode: 'segments',
            stages: [
              { at: 5500, color: '#30D158' },
              { at: 6600, color: '#FFCC00' },
              { at: 7200, color: '#FF3B30', blinkHz: 6 },
            ],
          },
        } satisfies ShiftLightWidget,

        {
          id: 'speed',
          type: 'numeric',
          rect: { x: 123, y: 150, w: 220, h: 100 },
          z: 1,
          signal: 'veh.speed',
          unit: 'km/h',
          props: { decimals: 0, align: 'center', showUnit: false },
        } satisfies NumericWidget,

        {
          id: 'speed-unit',
          type: 'label',
          rect: { x: 183, y: 248, w: 100, h: 22 },
          z: 1,
          props: { text: 'km/h', align: 'center', color: '#6E6E73' },
        } satisfies LabelWidget,

        {
          id: 'coolant',
          type: 'numeric',
          rect: { x: 55, y: 290, w: 130, h: 70 },
          z: 1,
          signal: 'engine.coolant_t',
          unit: '°C',
          props: {
            decimals: 0, align: 'center', caption: 'COOLANT',
            colorFromZones: true,
            zones: [
              { from: -40, to: 60, color: '#0A84FF' },
              { from: 60, to: 105, color: '#FFFFFF' },
              { from: 105, to: 150, color: '#FF3B30' },
            ],
          },
        } satisfies NumericWidget,

        {
          id: 'oil-p',
          type: 'numeric',
          rect: { x: 281, y: 290, w: 130, h: 70 },
          z: 1,
          signal: 'sensor.oil_p',
          unit: 'bar',
          props: {
            decimals: 1, align: 'center', caption: 'OIL',
            colorFromZones: true,
            zones: [
              { from: 0, to: 1, color: '#FF3B30' },
              { from: 1, to: 10, color: '#FFFFFF' },
            ],
          },
        } satisfies NumericWidget,

        {
          id: 'egt',
          type: 'numeric',
          rect: { x: 168, y: 370, w: 130, h: 56 },
          z: 1,
          signal: 'sensor.egt',
          unit: '°C',
          props: { decimals: 0, align: 'center', caption: 'EGT' },
        } satisfies NumericWidget,

        {
          id: 'bar-load',
          type: 'bar',
          rect: { x: 80, y: 430, w: 120, h: 8 },
          z: 0,
          signal: 'engine.load',
          props: { min: 0, max: 100, orientation: 'horizontal', radius: 4, color: '#FF9F0A', trackColor: '#1C1C1E' },
        } satisfies BarWidget,

        {
          id: 'bar-load-r',
          type: 'bar',
          rect: { x: 266, y: 430, w: 120, h: 8 },
          z: 0,
          signal: 'engine.load',
          props: { min: 0, max: 100, orientation: 'horizontal', radius: 4, color: '#FF9F0A', trackColor: '#1C1C1E' },
        } satisfies BarWidget,
      ],
    },
  ],
}

// ─── Типы состояния ───────────────────────────────────────────────────────────

interface HistoryEntry { layout: Layout }

interface EditorState {
  layout: Layout
  activeScreenIdx: number
  selectedWidgetId: string | null
  playing: boolean // мок-сигналы активны

  /// Превью с ограничениями устройства: гарнитура и кегли как в прошивке.
  /// Это настройка редактора, а не часть лейаута — в JSON не попадает.
  deviceMode: boolean

  // history
  history: HistoryEntry[]
  historyIdx: number

  // actions
  setLayout: (l: Layout) => void
  setActiveScreen: (idx: number) => void
  selectWidget: (id: string | null) => void
  togglePlaying: () => void
  toggleDeviceMode: () => void

  updateWidget: (screenIdx: number, widget: Widget) => void
  addWidget: (screenIdx: number, widget: Widget) => void
  removeWidget: (screenIdx: number, widgetId: string) => void
  /// Сдвинуть виджет в порядке z: dir +1 — выше, -1 — ниже
  moveWidgetZ: (screenIdx: number, widgetId: string, dir: 1 | -1) => void
  moveWidget: (screenIdx: number, widgetId: string, dx: number, dy: number) => void
  updateTheme: (patch: Partial<Theme>) => void
  addScreen: () => void
  removeScreen: (idx: number) => void
  renameScreen: (idx: number, name: string) => void
  setScreenBg: (idx: number, bg: string) => void
  setSimEnabled: (enabled: boolean) => void
  reorderScreens: (from: number, to: number) => void

  undo: () => void
  redo: () => void

  exportJSON: () => string
  importJSON: (json: string) => void
}

// ─── Хелперы ──────────────────────────────────────────────────────────────────

let _nextId = 1
export function newId(prefix: string) { return `${prefix}-${_nextId++}` }

function cloneLayout(l: Layout): Layout { return JSON.parse(JSON.stringify(l)) }

function withHistory(state: EditorState, newLayout: Layout): Partial<EditorState> {
  const trimmed = state.history.slice(0, state.historyIdx + 1)
  return {
    layout: newLayout,
    history: [...trimmed, { layout: cloneLayout(newLayout) }].slice(-50),
    historyIdx: trimmed.length,
  }
}

// ─── Store ────────────────────────────────────────────────────────────────────

export const useEditorStore = create<EditorState>((set, get) => ({
  layout: cloneLayout(STARTER_LAYOUT),
  activeScreenIdx: 0,
  selectedWidgetId: null,
  playing: true,
  deviceMode: false,
  history: [{ layout: cloneLayout(STARTER_LAYOUT) }],
  historyIdx: 0,

  setLayout: (l) => set(withHistory(get(), l)),
  setActiveScreen: (idx) => set({ activeScreenIdx: idx, selectedWidgetId: null }),
  selectWidget: (id) => set({ selectedWidgetId: id }),
  togglePlaying: () => set((s) => ({ playing: !s.playing })),
  toggleDeviceMode: () => set((s) => ({ deviceMode: !s.deviceMode })),

  updateWidget: (si, widget) => set((s) => {
    const l = cloneLayout(s.layout)
    const ws = l.screens[si].widgets
    const idx = ws.findIndex(w => w.id === widget.id)
    if (idx >= 0) ws[idx] = widget
    return withHistory(s, l)
  }),

  addWidget: (si, widget) => set((s) => {
    const l = cloneLayout(s.layout)
    l.screens[si].widgets.push(widget)
    return { ...withHistory(s, l), selectedWidgetId: widget.id }
  }),

  removeWidget: (si, widgetId) => set((s) => {
    const l = cloneLayout(s.layout)
    l.screens[si].widgets = l.screens[si].widgets.filter(w => w.id !== widgetId)
    return { ...withHistory(s, l), selectedWidgetId: null }
  }),

  moveWidgetZ: (si, widgetId, dir) => set((s) => {
    const l = cloneLayout(s.layout)
    const ws = l.screens[si].widgets

    // Порядок отрисовки: по z, при равных z — по позиции в массиве
    const order = ws
      .map((w, i) => ({ w, i }))
      .sort((a, b) => (a.w.z ?? 0) - (b.w.z ?? 0) || a.i - b.i)

    const at = order.findIndex(o => o.w.id === widgetId)
    const to = at + dir
    if (at < 0 || to < 0 || to >= order.length) return s

    ;[order[at], order[to]] = [order[to], order[at]]

    // z перенумеровываем подряд по всему экрану. Обмена двух значений мало:
    // у соседей z нередко совпадают, и тогда обмен ничего бы не изменил.
    order.forEach((o, k) => { o.w.z = k })

    return withHistory(s, l)
  }),

  moveWidget: (si, widgetId, dx, dy) => set((s) => {
    const l = cloneLayout(s.layout)
    const w = l.screens[si].widgets.find(w => w.id === widgetId)
    if (w) { w.rect.x = Math.round(w.rect.x + dx); w.rect.y = Math.round(w.rect.y + dy) }
    return withHistory(s, l)
  }),

  updateTheme: (patch) => set((s) => {
    const l = cloneLayout(s.layout)
    l.theme = { ...l.theme, ...patch }
    return withHistory(s, l)
  }),

  addScreen: () => set((s) => {
    const l = cloneLayout(s.layout)
    const id = newId('screen')
    l.screens.push({ id, name: `Screen ${l.screens.length + 1}`, widgets: [] })
    return { ...withHistory(s, l), activeScreenIdx: l.screens.length - 1 }
  }),

  removeScreen: (idx) => set((s) => {
    if (s.layout.screens.length <= 1) return {}
    const l = cloneLayout(s.layout)
    l.screens.splice(idx, 1)
    return { ...withHistory(s, l), activeScreenIdx: Math.min(idx, l.screens.length - 1) }
  }),

  reorderScreens: (from, to) => set((s) => {
    if (from === to) return {}
    const l = cloneLayout(s.layout)
    const [moved] = l.screens.splice(from, 1)
    l.screens.splice(to, 0, moved)
    // keep active screen pointing to same screen object
    const newActive = to < from
      ? s.activeScreenIdx === from ? to : s.activeScreenIdx < to ? s.activeScreenIdx : s.activeScreenIdx + 1
      : s.activeScreenIdx === from ? to : s.activeScreenIdx > to ? s.activeScreenIdx : s.activeScreenIdx - 1
    return { ...withHistory(s, l), activeScreenIdx: Math.max(0, Math.min(l.screens.length - 1, newActive)) }
  }),

  renameScreen: (idx, name) => set((s) => {
    const l = cloneLayout(s.layout)
    l.screens[idx].name = name
    return withHistory(s, l)
  }),

  setScreenBg: (idx, bg) => set((s) => {
    const l = cloneLayout(s.layout)
    l.screens[idx].bg = bg as Screen['bg']
    return withHistory(s, l)
  }),

  setSimEnabled: (enabled) => set((s) => {
    const l = cloneLayout(s.layout)
    l.sim = { enabled }
    return withHistory(s, l)
  }),

  undo: () => set((s) => {
    const idx = Math.max(0, s.historyIdx - 1)
    return { layout: cloneLayout(s.history[idx].layout), historyIdx: idx }
  }),

  redo: () => set((s) => {
    const idx = Math.min(s.history.length - 1, s.historyIdx + 1)
    return { layout: cloneLayout(s.history[idx].layout), historyIdx: idx }
  }),

  exportJSON: () => JSON.stringify(get().layout, null, 2),

  importJSON: (json) => {
    try {
      const l = JSON.parse(json) as Layout
      set((s) => ({ ...withHistory(s, l), activeScreenIdx: 0, selectedWidgetId: null }))
    } catch { /* ignore */ }
  },
}))

// ─── Удобные селекторы ────────────────────────────────────────────────────────

export const useActiveScreen = (): Screen =>
  useEditorStore(s => s.layout.screens[s.activeScreenIdx])

export const useSelectedWidget = (): Widget | null =>
  useEditorStore(s => {
    const scr = s.layout.screens[s.activeScreenIdx]
    return scr?.widgets.find(w => w.id === s.selectedWidgetId) ?? null
  })
