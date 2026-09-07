/**
 * WidgetPalette — левая панель с типами виджетов.
 * Клик — добавляет виджет в центр активного экрана.
 */

import { useEditorStore } from '@/store'
import { Widget, WidgetType } from '@/schema/layout'
import { newId } from '@/store'

interface PaletteItem {
  type: WidgetType
  label: string
  icon: string
  defaultWidget: () => Omit<Widget, 'id'>
}

const PALETTE: PaletteItem[] = [
  {
    type: 'numeric', label: 'Numeric', icon: '🔢',
    defaultWidget: () => ({
      type: 'numeric',
      rect: { x: 158, y: 180, w: 150, h: 80 },
      z: 1,
      signal: 'veh.speed',
      unit: 'km/h',
      props: { decimals: 0, fontSize: 0, align: 'center', showUnit: true, color: '#FFFFFF' },
    }),
  },
  {
    type: 'label', label: 'Label', icon: 'T',
    defaultWidget: () => ({
      type: 'label',
      rect: { x: 183, y: 220, w: 100, h: 24 },
      z: 1,
      props: { text: 'Label', fontSize: 0, align: 'center', color: '#6E6E73' },
    }),
  },
  {
    type: 'bar', label: 'Bar', icon: '▬',
    defaultWidget: () => ({
      type: 'bar',
      rect: { x: 80, y: 420, w: 140, h: 10 },
      z: 0,
      signal: 'engine.load',
      props: { min: 0, max: 100, orientation: 'horizontal', color: '#FF9F0A', trackColor: '#1C1C1E' },
    }),
  },
  {
    type: 'shift_light', label: 'Shift Segments', icon: '●●●',
    defaultWidget: () => ({
      type: 'shift_light',
      rect: { x: 133, y: 40, w: 200, h: 14 },
      z: 2,
      signal: 'engine.rpm',
      props: {
        mode: 'segments',
        stages: [
          { at: 5500, color: '#30D158' },
          { at: 6600, color: '#FFCC00' },
          { at: 7200, color: '#FF3B30', blinkHz: 6 },
        ],
      },
    }),
  },
  {
    type: 'shift_light', label: 'Shift Arc', icon: '◜●◝',
    defaultWidget: () => ({
      type: 'shift_light',
      rect: { x: 83, y: 26, w: 300, h: 60 },
      z: 2,
      signal: 'engine.rpm',
      props: {
        mode: 'arc',
        stages: [
          { at: 5000, color: '#30D158' },
          { at: 5800, color: '#34C759' },
          { at: 6200, color: '#FFCC00' },
          { at: 6600, color: '#FF9F0A' },
          { at: 7000, color: '#FF453A' },
          { at: 7200, color: '#FF3B30', blinkHz: 8 },
        ],
      },
    }),
  },
  {
    type: 'shift_light', label: 'Shift Flash', icon: '⚡',
    defaultWidget: () => ({
      type: 'shift_light',
      rect: { x: 0, y: 0, w: 466, h: 466 },
      z: 5,
      signal: 'engine.rpm',
      props: {
        mode: 'flash',
        stages: [
          { at: 7000, color: '#FF9F0A' },
          { at: 7200, color: '#FF3B30', blinkHz: 8 },
        ],
      },
    }),
  },
  {
    type: 'warning', label: 'Warning', icon: '⚠',
    defaultWidget: () => ({
      type: 'warning',
      rect: { x: 0, y: 0, w: 466, h: 466 },
      z: 10,
      signal: 'sensor.oil_p',
      unit: 'bar',
      props: {
        icon: 'oil',
        color: '#FF3B30',
        label: 'LOW OIL',
        triggerBelow: 1.5,
      },
    }),
  },
  {
    type: 'graph', label: 'Graph', icon: '📈',
    defaultWidget: () => ({
      type: 'graph',
      rect: { x: 60, y: 100, w: 346, h: 180 },
      z: 0,
      props: { min: 0, max: 150, windowSec: 30, lineWidth: 2, signals: [{ signal: 'engine.coolant_t', color: '#0A84FF' }] },
    }),
  },
  {
    type: 'gforce', label: 'G-Force', icon: '⊕',
    defaultWidget: () => ({
      type: 'gforce',
      rect: { x: 173, y: 280, w: 120, h: 120 },
      z: 0,
      props: { range: 1.5, rings: 3, signalX: 'imu.ax', signalY: 'imu.ay' },
    }),
  },
  {
    type: 'steering', label: 'Steering', icon: '⇄',
    defaultWidget: () => ({
      type: 'steering',
      rect: { x: 133, y: 420, w: 200, h: 14 },
      z: 1,
      signal: 'steer.pos',
      props: {
        color: '#0A84FF',
        trackColor: '#1C1C1E',
        radius: 4,
        centerMark: true,
        showValue: false,
        maxAngle: 450,
        deadzone: 0,
      },
    }),
  },
  {
    type: 'clock', label: 'Clock', icon: '🕐',
    defaultWidget: () => ({
      type: 'clock',
      rect: { x: 163, y: 196, w: 140, h: 50 },
      z: 1,
      props: { format: '24h', showSeconds: false, color: '#FFFFFF' },
    }),
  },
  {
    type: 'lap_timer', label: 'Lap Timer', icon: '⏱',
    defaultWidget: () => ({
      type: 'lap_timer',
      rect: { x: 138, y: 190, w: 190, h: 60 },
      z: 1,
      props: { color: '#FFFFFF', bestColor: '#30D158', showBest: true, showDelta: true },
    }),
  },
]

export function WidgetPalette() {
  const { activeScreenIdx, addWidget } = useEditorStore()

  function handleAdd(item: PaletteItem) {
    const partial = item.defaultWidget()
    const widget = { ...partial, id: newId(item.type) } as Widget
    addWidget(activeScreenIdx, widget)
  }

  return (
    <div className="palette">
      <div className="panel-title">Widgets</div>
      <div className="palette-list">
        {PALETTE.map(item => (
          <button
            key={item.label}
            className="palette-item"
            onClick={() => handleAdd(item)}
            title={`Add ${item.label}`}
          >
            <span className="palette-icon">{item.icon}</span>
            <span className="palette-label">{item.label}</span>
          </button>
        ))}
      </div>
    </div>
  )
}
