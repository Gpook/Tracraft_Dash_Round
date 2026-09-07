/**
 * LayoutTemplates — модальный выбор пресет-лейаута для нового экрана.
 *
 * Каждый шаблон — набор виджетов, преднастроенных под круглый 466×466 дисплей.
 * После выбора шаблона виджеты вставляются в текущий активный экран, заменяя
 * всё содержимое (с предупреждением, если экран не пустой).
 */

import { useEditorStore } from '@/store'
import { Widget, NumericWidget, ShiftLightWidget, LabelWidget, BarWidget, GForceWidget, GraphWidget } from '@/schema/layout'
import { newId } from '@/store'

interface Template {
  id: string
  name: string
  description: string
  icon: string
  widgets: () => Omit<Widget, 'id'>[]
}

// ─── Шаблоны ──────────────────────────────────────────────────────────────────
// Все координаты рассчитаны для 466×466.

const TEMPLATES: Template[] = [
  {
    id: 'blank',
    name: 'Blank',
    description: 'Empty screen — total freedom',
    icon: '⬜',
    widgets: () => [],
  },
  {
    id: 'speed-focus',
    name: 'Speed Focus',
    description: 'Shift lights + centered speed',
    icon: '◉',
    widgets: () => [
        { type: 'numeric', rect: { x: 133, y: 86, w: 200, h: 60 }, z: 1, signal: 'engine.rpm',
          unit: 'rpm', props: { decimals: 0, align: 'center', showUnit: true, colorFromZones: true,
            zones: [{ from: 6600, to: 7200, color: '#FFCC00' }, { from: 7200, to: 8000, color: '#FF3B30' }] },
        } satisfies Omit<NumericWidget, 'id'>,
      { type: 'shift_light', rect: { x: 133, y: 46, w: 200, h: 14 }, z: 1, signal: 'engine.rpm',
        props: { mode: 'segments', stages: [
          { at: 5500, color: '#30D158' }, { at: 6600, color: '#FFCC00' }, { at: 7200, color: '#FF3B30', blinkHz: 6 },
        ] },
      } satisfies Omit<ShiftLightWidget, 'id'>,
      { type: 'numeric', rect: { x: 113, y: 148, w: 240, h: 110 }, z: 1, signal: 'veh.speed',
        unit: 'km/h', props: { decimals: 0, align: 'center', showUnit: false },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'label', rect: { x: 183, y: 256, w: 100, h: 20 }, z: 1,
        props: { text: 'km/h', align: 'center', color: '#6E6E73' },
      } satisfies Omit<LabelWidget, 'id'>,
      { type: 'numeric', rect: { x: 55, y: 292, w: 130, h: 70 }, z: 1, signal: 'engine.coolant_t',
        unit: '°C', props: { decimals: 0, align: 'center', caption: 'COOLANT',
          colorFromZones: true, zones: [{ from: -40, to: 60, color: '#0A84FF' }, { from: 60, to: 105, color: '#FFF' }, { from: 105, to: 150, color: '#FF3B30' }] },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 281, y: 292, w: 130, h: 70 }, z: 1, signal: 'sensor.oil_p',
        unit: 'bar', props: { decimals: 1, align: 'center', caption: 'OIL P',
          colorFromZones: true, zones: [{ from: 0, to: 1, color: '#FF3B30' }, { from: 1, to: 10, color: '#FFF' }] },
      } satisfies Omit<NumericWidget, 'id'>,
    ],
  },
  {
    id: 'quad',
    name: '4 Metrics',
    description: '4 large numeric fields in quadrants',
    icon: '⊞',
    widgets: () => {
      // Квадранты в круге 466: центр 233,233. Рабочая область ~±160px от центра
      const fields: [string, string, string][] = [
        ['engine.coolant_t', '°C', 'COOLANT'],
        ['sensor.oil_p',     'bar', 'OIL P'],
        ['engine.rpm',       'rpm', 'RPM'],
        ['veh.speed',        'km/h', 'SPEED'],
      ]
      const positions = [
        { x: 60,  y: 80  }, // TL
        { x: 266, y: 80  }, // TR
        { x: 60,  y: 296 }, // BL
        { x: 266, y: 296 }, // BR
      ]
      return fields.map((f, i) => ({
        type: 'numeric' as const,
        rect: { x: positions[i].x, y: positions[i].y, w: 140, h: 90 },
        z: 1, signal: f[0], unit: f[1],
        props: { decimals: f[1] === 'bar' ? 1 : 0, align: 'center', caption: f[2] },
      }) satisfies Omit<NumericWidget, 'id'>)
    },
  },
  {
    id: 'track',
    name: 'Track',
    description: 'RPM arc + shift + 3 data fields',
    icon: '🏎',
    widgets: () => [
        { type: 'numeric', rect: { x: 133, y: 86, w: 200, h: 60 }, z: 1, signal: 'engine.rpm',
          unit: 'rpm', props: { decimals: 0, align: 'center', showUnit: true, colorFromZones: true,
            zones: [{ from: 6600, to: 7200, color: '#FFCC00' }, { from: 7200, to: 8000, color: '#FF3B30' }] },
        } satisfies Omit<NumericWidget, 'id'>,
      { type: 'shift_light', rect: { x: 133, y: 46, w: 200, h: 12 }, z: 2, signal: 'engine.rpm',
        props: { mode: 'segments', stages: [
          { at: 5500, color: '#30D158' }, { at: 6200, color: '#FFCC00' },
          { at: 6800, color: '#FF9F0A' }, { at: 7200, color: '#FF3B30', blinkHz: 8 },
        ] },
      } satisfies Omit<ShiftLightWidget, 'id'>,
      { type: 'numeric', rect: { x: 133, y: 148, w: 200, h: 80 }, z: 1, signal: 'veh.speed',
        unit: 'km/h', props: { decimals: 0, align: 'center', showUnit: false },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 64, y: 300, w: 110, h: 64 }, z: 1, signal: 'engine.coolant_t',
        unit: '°C', props: { decimals: 0, align: 'center', caption: 'COOLANT' },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 178, y: 352, w: 110, h: 64 }, z: 1, signal: 'sensor.egt',
        unit: '°C', props: { decimals: 0, align: 'center', caption: 'EGT' },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 292, y: 300, w: 110, h: 64 }, z: 1, signal: 'sensor.oil_p',
        unit: 'bar', props: { decimals: 1, align: 'center', caption: 'OIL P' },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'bar', rect: { x: 80, y: 430, w: 120, h: 8 }, z: 0, signal: 'engine.load',
        props: { min: 0, max: 100, orientation: 'horizontal', color: '#FF9F0A', trackColor: '#222' },
      } satisfies Omit<BarWidget, 'id'>,
      { type: 'bar', rect: { x: 266, y: 430, w: 120, h: 8 }, z: 0, signal: 'engine.load',
        props: { min: 0, max: 100, orientation: 'horizontal', color: '#FF9F0A', trackColor: '#222' },
      } satisfies Omit<BarWidget, 'id'>,
    ],
  },
  {
    id: 'temps',
    name: 'Temps & Graph',
    description: 'Scrolling temperature graph + 2 fields',
    icon: '📈',
    widgets: () => [
      { type: 'graph', rect: { x: 56, y: 80, w: 354, h: 200 }, z: 0,
        props: { min: 0, max: 140, windowSec: 60, lineWidth: 2,
          signals: [{ signal: 'engine.coolant_t', color: '#0A84FF' }, { signal: 'sensor.oil_t', color: '#FFCC00' }] },
      } satisfies Omit<GraphWidget, 'id'>,
      { type: 'numeric', rect: { x: 64, y: 310, w: 150, h: 80 }, z: 1, signal: 'engine.coolant_t',
        unit: '°C', props: { decimals: 0, align: 'center', caption: 'COOLANT',
          colorFromZones: true, zones: [{ from: -40, to: 60, color: '#0A84FF' }, { from: 60, to: 105, color: '#FFF' }, { from: 105, to: 150, color: '#FF3B30' }] },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 252, y: 310, w: 150, h: 80 }, z: 1, signal: 'sensor.oil_t',
        unit: '°C', props: { decimals: 0, align: 'center', caption: 'OIL TEMP' },
      } satisfies Omit<NumericWidget, 'id'>,
    ],
  },
  {
    id: 'gforce',
    name: 'G-Force',
    description: 'G-ball + speed + 2 temperatures',
    icon: '⊕',
    widgets: () => [
      { type: 'gforce', rect: { x: 133, y: 80, w: 200, h: 200 }, z: 0,
        props: { range: 1.5, rings: 3, trail: true, signalX: 'imu.ax', signalY: 'imu.ay' },
      } satisfies Omit<GForceWidget, 'id'>,
      { type: 'numeric', rect: { x: 148, y: 296, w: 170, h: 72 }, z: 1, signal: 'veh.speed',
        unit: 'km/h', props: { decimals: 0, align: 'center', showUnit: false },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 64, y: 360, w: 140, h: 64 }, z: 1, signal: 'engine.coolant_t',
        unit: '°C', props: { decimals: 0, align: 'center', caption: 'COOLANT' },
      } satisfies Omit<NumericWidget, 'id'>,
      { type: 'numeric', rect: { x: 262, y: 360, w: 140, h: 64 }, z: 1, signal: 'sys.battery',
        unit: 'V', props: { decimals: 1, align: 'center', caption: 'BATT' },
      } satisfies Omit<NumericWidget, 'id'>,
    ],
  },
]

// ─── Компонент ────────────────────────────────────────────────────────────────

interface Props { onClose: () => void }

export function LayoutTemplates({ onClose }: Props) {
  const { activeScreenIdx, layout } = useEditorStore()
  const store = useEditorStore()

  function apply(tpl: Template) {
    const currentWidgets = layout.screens[activeScreenIdx]?.widgets ?? []
    if (currentWidgets.length > 0) {
      const ok = window.confirm(`Replace ${currentWidgets.length} widget(s) on this screen with template "${tpl.name}"?`)
      if (!ok) return
    }

    // Создаём копию экрана с новыми виджетами
    const widgets: Widget[] = tpl.widgets().map(w => ({ ...w, id: newId(w.type) } as Widget))
    const screen = { ...layout.screens[activeScreenIdx], widgets }
    const newScreens = [...layout.screens]
    newScreens[activeScreenIdx] = screen
    store.setLayout({ ...layout, screens: newScreens })
    onClose()
  }

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal" onClick={e => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Layout Templates</h2>
          <button className="modal-close" onClick={onClose}>✕</button>
        </div>
        <div className="templates-grid">
          {TEMPLATES.map(tpl => (
            <button key={tpl.id} className="template-card" onClick={() => apply(tpl)}>
              <span className="template-icon">{tpl.icon}</span>
              <span className="template-name">{tpl.name}</span>
              <span className="template-desc">{tpl.description}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  )
}
