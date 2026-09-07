/**
 * PropertiesPanel — правая панель инспектора.
 *
 * ИСПРАВЛЕНИЯ:
 *  1. Drag/resize сброс: widgetRef (актуальный после каждого рендера) +
 *     JSON-сравнение результата с текущим виджетом перед onUpdate
 *  2. Shift Light: скрыта секция Signal и Mode; dotSize только для arc
 *  3. Warning: убран blinkHz из панели
 *  4. Graph: редактор сигналов с direct-onUpdate; props.signals скипается в unflattenWidget
 *  5. Bar orientation: авто-своп w↔h
 */

import { useEffect, useRef } from 'react'
import { useForm, useWatch } from 'react-hook-form'
import { useEditorStore, useSelectedWidget } from '@/store'
import { Widget } from '@/schema/layout'

const SIGNALS = [
  'engine.rpm', 'engine.coolant_t', 'engine.load',
  'engine.map', 'engine.timing', 'engine.iat',
  'veh.speed', 'veh.gear',
  'sensor.oil_p', 'sensor.oil_t', 'sensor.egt',
  'sys.battery',
  'imu.ax', 'imu.ay',
  'pedal.brake', 'pedal.accel', 'steer.pos',
  // Средние по колёсам. Отдельные секторы (tire.fl.t1 … tire.rr.t4) в список
  // не вошли: их двадцать, и выбирать их по одному незачем — виджет Tire Temp
  // собирает имена сам из префикса. А среднее пригодится в графике и цифрах.
  'tire.fl.avg', 'tire.fr.avg', 'tire.rl.avg', 'tire.rr.avg',
  'calc.oil_p_margin',
]

// ─── Корневой компонент ───────────────────────────────────────────────────────

export function PropertiesPanel() {
  const selected = useSelectedWidget()
  const {
    layout, activeScreenIdx, updateWidget, removeWidget,
    selectedWidgetId, renameScreen, setScreenBg,
  } = useEditorStore()

  if (!selected || !selectedWidgetId) {
    const screen = layout.screens[activeScreenIdx]
    const bg = screen?.bg ?? layout.theme.bg
    return (
      <div className="props-panel">
        <div className="panel-title">Properties</div>
        {screen && (
          <div className="props-form">
            <section>
              <div className="props-section-title">Screen</div>
              <label>Name
                <input
                  type="text"
                  defaultValue={screen.name ?? `Screen ${activeScreenIdx + 1}`}
                  onBlur={e => renameScreen(activeScreenIdx, e.target.value)}
                  onKeyDown={e => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur() }}
                />
              </label>
              <label>Background
                <input
                  type="color"
                  value={bg}
                  onChange={e => setScreenBg(activeScreenIdx, e.target.value)}
                />
              </label>
              <div className="props-hint">
                {screen.bg
                  ? 'Переопределяет фон темы для этого экрана'
                  : `Наследует тему (${layout.theme.bg})`}
              </div>
              {screen.bg && (
                <button
                  type="button"
                  onClick={() => setScreenBg(activeScreenIdx, layout.theme.bg)}
                  style={{ fontSize: '11px', padding: '3px 8px', marginTop: '4px' }}
                >Сбросить к теме</button>
              )}
            </section>
          </div>
        )}
      </div>
    )
  }

  return (
    <div className="props-panel">
      <div className="panel-title">
        <span>{selected.type}</span>
        <button
          className="btn-danger-sm"
          onClick={() => removeWidget(activeScreenIdx, selectedWidgetId)}
          title="Delete (Del)"
        >✕</button>
      </div>
      <WidgetForm
        key={selected.id}
        widget={selected}
        onUpdate={w => updateWidget(activeScreenIdx, w)}
      />
    </div>
  )
}

// ─── Форма ────────────────────────────────────────────────────────────────────

interface FormProps { widget: Widget; onUpdate: (w: Widget) => void }

function WidgetForm({ widget, onUpdate }: FormProps) {
  const { register, control, reset, setValue } = useForm({
    defaultValues: flattenWidget(widget),
  })

  // Полный сброс только при смене виджета (другой id)
  useEffect(() => { reset(flattenWidget(widget)) }, [widget.id]) // eslint-disable-line

  // ── Всегда актуальная ссылка на виджет из стора ──────────────────────────
  // ВАЖНО: объявлена ПЕРЕД всеми useEffect(, [values]) чтобы обновляться раньше!
  const widgetRef = useRef(widget)
  useEffect(() => { widgetRef.current = widget })  // no deps → каждый рендер

  // ── Синхронизация rect/z из canvas drag/resize → в форму ─────────────────
  const prevRectRef = useRef(widget.rect)
  const prevZRef    = useRef(widget.z ?? 0)
  useEffect(() => {
    const r = widget.rect, z = widget.z ?? 0
    if (r.x !== prevRectRef.current.x) setValue('rect.x' as any, r.x)
    if (r.y !== prevRectRef.current.y) setValue('rect.y' as any, r.y)
    if (r.w !== prevRectRef.current.w) setValue('rect.w' as any, r.w)
    if (r.h !== prevRectRef.current.h) setValue('rect.h' as any, r.h)
    if (z   !== prevZRef.current)      setValue('z' as any, z)
    prevRectRef.current = r
    prevZRef.current = z
  }, [widget.rect.x, widget.rect.y, widget.rect.w, widget.rect.h, widget.z, setValue])

  // ── Live-обновление с двойной защитой ────────────────────────────────────
  //  1. JSON-сравнение значений формы → не перезапускаем если форма не изменилась
  //  2. JSON-сравнение результата с актуальным виджетом → drag не сбрасывается!
  //     (stale form values, rect.x=110, пока canvas уже на x=120 → SKIP)
  const values = useWatch({ control })
  const lastJsonRef = useRef<string>('')

  useEffect(() => {
    const json = JSON.stringify(values)
    if (json === lastJsonRef.current) return
    lastJsonRef.current = json

    // widgetRef.current обновлён на этом же рендере в эффекте выше
    const current = widgetRef.current
    const result  = unflattenWidget(current, values as Record<string, unknown>)
    if (!result) return

    // Bar: смена orientation меняет местами w↔h. Делаем в одном апдейте с
    // самой ориентацией, иначе второй onUpdate прочитает устаревший widgetRef.
    const prevOrient = (current as any).props?.orientation
    const nextOrient = (result  as any).props?.orientation
    if (current.type === 'bar' && prevOrient && nextOrient && prevOrient !== nextOrient) {
      result.rect = { ...result.rect, w: current.rect.h, h: current.rect.w }
    }

    // Ничего не изменилось — не плодим записи в истории
    if (JSON.stringify(result) === JSON.stringify(current)) return

    onUpdate(result)
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [values])

  // ── Ручной ввод rect/z: коммит по blur или Enter ──────────────────────────
  // Живой путь формы rect не трогает (см. unflattenWidget), поэтому применяем явно.
  function commitRect(field: 'x' | 'y' | 'w' | 'h' | 'z', raw: string) {
    const n = Number(raw)
    if (!isFinite(n)) return
    const cur = widgetRef.current
    if (field === 'z') {
      if ((cur.z ?? 0) === n) return
      onUpdate({ ...cur, z: n })
    } else {
      if (cur.rect[field] === n) return
      onUpdate({ ...cur, rect: { ...cur.rect, [field]: n } })
    }
  }

  // Оборачиваем register чтобы сохранить onBlur самого RHF и добавить свой коммит
  function rectField(name: 'rect.x' | 'rect.y' | 'rect.w' | 'rect.h' | 'z',
                     field: 'x' | 'y' | 'w' | 'h' | 'z') {
    const reg = register(name as any, { valueAsNumber: true })
    return {
      ...reg,
      onBlur: (e: React.FocusEvent<HTMLInputElement>) => {
        reg.onBlur(e)
        commitRect(field, e.target.value)
      },
      onKeyDown: (e: React.KeyboardEvent<HTMLInputElement>) => {
        if (e.key === 'Enter') { e.preventDefault(); e.currentTarget.blur() }
      },
    }
  }

  // Shift light — signal всегда engine.rpm, менять не нужно
  // tire_temp тоже без поля signal: его двадцать сигналов собираются из
  // префикса, отдельного «главного» среди них нет.
  const hideSignal = ['label', 'image', 'gforce', 'graph', 'shift_light', 'tire_temp'].includes(widget.type)

  return (
    <form className="props-form" onSubmit={e => e.preventDefault()}>
      {/* ID */}
      <section>
        <label>ID <input {...register('id')} readOnly className="input-readonly" /></label>
      </section>

      {/* Position & Size */}
      <section>
        <div className="props-section-title">Position & Size</div>
        <div className="props-grid-4">
          <label>X<input type="number" {...rectField('rect.x', 'x')} /></label>
          <label>Y<input type="number" {...rectField('rect.y', 'y')} /></label>
          <label>W<input type="number" {...rectField('rect.w', 'w')} /></label>
          <label>H<input type="number" {...rectField('rect.h', 'h')} /></label>
        </div>
        <label>Z-order <input type="number" {...rectField('z', 'z')} /></label>
        <div className="props-hint">Enter / клик вне поля — применить</div>
      </section>

      {/* Signal / Data */}
      {!hideSignal && (
        <section>
          <div className="props-section-title">Data</div>
          <label>Signal
            <select {...register('signal')}>
              <option value="">— none —</option>
              {SIGNALS.map(s => <option key={s} value={s}>{s}</option>)}
            </select>
          </label>
          <label>Unit <input {...register('unit')} placeholder="km/h, °C, bar…" /></label>
        </section>
      )}

      {/* Type-specific секции */}
      {widget.type === 'numeric'     && <NumericFields register={register} />}
      {widget.type === 'label'       && <LabelFields register={register} />}
      {widget.type === 'bar'         && <BarFields register={register} />}
      {widget.type === 'shift_light' && <ShiftLightFields register={register} widget={widget} />}
      {widget.type === 'warning'     && <WarningFields register={register} />}
      {widget.type === 'gforce'      && <GForceFields register={register} />}
      {widget.type === 'steering'    && <SteeringFields register={register} />}
      {widget.type === 'tire_temp'   && <TireTempFields register={register} />}
      {widget.type === 'graph'       && <GraphFields register={register} widget={widget} onUpdate={onUpdate} />}
      {widget.type === 'clock'       && <ClockFields register={register} />}
    </form>
  )
}

// ─── Секции полей ─────────────────────────────────────────────────────────────

function NumericFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Numeric</div>
      <label>Decimals<input type="number" min={0} max={3} {...register('props.decimals', { valueAsNumber: true })} /></label>
      <FontSizeField register={register} />
      <label>Caption<input {...register('props.caption')} placeholder="OIL, COOLANT…" /></label>
      <label>Align
        <select {...register('props.align')}>
          <option value="left">Left</option>
          <option value="center">Center</option>
          <option value="right">Right</option>
        </select>
      </label>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.showUnit')} /> Show unit</label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.colorFromZones')} /> Color from zones</label>
    </section>
  )
}

function LabelFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Label</div>
      <label>Text<input {...register('props.text')} /></label>
      <FontSizeField register={register} />
      <label>Align
        <select {...register('props.align')}>
          <option value="left">Left</option>
          <option value="center">Center</option>
          <option value="right">Right</option>
        </select>
      </label>
      <label>Color<input type="color" {...register('props.color')} /></label>
    </section>
  )
}

function BarFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Bar</div>
      <div className="props-grid-2">
        <label>Min<input type="number" {...register('props.min', { valueAsNumber: true })} /></label>
        <label>Max<input type="number" {...register('props.max', { valueAsNumber: true })} /></label>
      </div>
      <label>Orientation
        <select {...register('props.orientation')}>
          <option value="horizontal">Horizontal</option>
          <option value="vertical">Vertical</option>
        </select>
      </label>
      <div className="props-hint">W↔H auto-swapped on orientation change</div>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <label>Track color<input type="color" {...register('props.trackColor')} /></label>
    </section>
  )
}

// Mode скрыт — определён палитрой. dotSize только для arc.
function ShiftLightFields({ register, widget }: { register: any; widget: Widget }) {
  const currentMode: string = (widget as any).props?.mode ?? 'segments'
  return (
    <section>
      <div className="props-section-title">Shift Light</div>
      <div className="props-hint" style={{ marginBottom: '6px' }}>
        Mode: <strong>{currentMode}</strong>
      </div>
      {currentMode === 'arc' && (
        <label>
          Dot size <small style={{ color: '#888' }}>(0 = auto)</small>
          <input
            type="number" min={0} max={30} step={1}
            placeholder="0 = auto"
            {...register('props.dotSize', { valueAsNumber: true })}
          />
        </label>
      )}
      {currentMode === 'flash' && (
        <div className="props-hint">Rect 0,0,466,466 для full-screen</div>
      )}
      <div className="props-hint">Stages (at, color, blinkHz) — JSON export</div>
    </section>
  )
}

function WarningFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Warning</div>
      <label>Icon
        <select {...register('props.icon')}>
          {['oil', 'temp', 'battery', 'check_engine', 'fuel', 'brake', 'generic'].map(i =>
            <option key={i} value={i}>{i}</option>
          )}
        </select>
      </label>
      <label>Label<input {...register('props.label')} placeholder="LOW OIL, HIGH TEMP…" /></label>
      <label>Trigger below<input type="number" step="0.1" {...register('props.triggerBelow', { valueAsNumber: true })} placeholder="e.g. 1.5" /></label>
      <label>Trigger above<input type="number" step="0.1" {...register('props.triggerAbove', { valueAsNumber: true })} placeholder="e.g. 110" /></label>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <div className="props-hint">
        Появляется на 2 с при срабатывании триггера.<br />
        Требует Signal + Trigger below/above.
      </div>
    </section>
  )
}

function GForceFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">G-Force</div>
      <label>Range (G)<input type="number" step="0.5" min={0.5} max={5} {...register('props.range', { valueAsNumber: true })} /></label>
      <label>Rings<input type="number" min={1} max={5} {...register('props.rings', { valueAsNumber: true })} /></label>
      <label>Signal X (lateral)
        <select {...register('props.signalX')}>
          {SIGNALS.map(s => <option key={s} value={s}>{s}</option>)}
        </select>
      </label>
      <label>Signal Y (longitudinal)
        <select {...register('props.signalY')}>
          {SIGNALS.map(s => <option key={s} value={s}>{s}</option>)}
        </select>
      </label>
    </section>
  )
}

/// Кегль для Label и Numeric. Прошивка масштабирует шрифт дробно, так что
/// любое значение воспроизводится точно — ограничений по набору размеров нет.
function FontSizeField({ register }: { register: any }) {
  return (
    <>
      <label>Font size
        <input
          type="number" min={0} max={200}
          {...register('props.fontSize', { valueAsNumber: true })}
        />
      </label>
      <div className="props-hint">
        0 = авто: текст растёт вслед за рамкой.
      </div>
    </>
  )
}

function SteeringFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Steering</div>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <label>Track color<input type="color" {...register('props.trackColor')} /></label>
      <label>Corner radius<input type="number" min={0} max={20} {...register('props.radius', { valueAsNumber: true })} /></label>
      <label>Max angle (°)<input type="number" min={90} max={1080} step={10} {...register('props.maxAngle', { valueAsNumber: true })} /></label>
      <label>Deadzone<input type="number" min={0} max={0.5} step={0.01} {...register('props.deadzone', { valueAsNumber: true })} /></label>
      <div className="props-hint">Зона нечувствительности вокруг центра, 0…0.5</div>
      <label className="checkbox-row"><input type="checkbox" {...register('props.centerMark')} /> Center mark</label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.showValue')} /> Show angle</label>
    </section>
  )
}

function TireTempFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Tire Temp</div>
      <label>Signal prefix<input {...register('props.prefix')} /></label>
      <div className="props-hint">Имена собираются как prefix.fl.t1 … prefix.rr.t4</div>
      <div className="props-grid-2">
        <label>Min °C<input type="number" step={5} {...register('props.min', { valueAsNumber: true })} /></label>
        <label>Max °C<input type="number" step={5} {...register('props.max', { valueAsNumber: true })} /></label>
      </div>
      <div className="props-hint">Min — синий, середина — зелёный, Max — красный</div>
      <div className="props-grid-2">
        <label>Gap X<input type="number" min={0} max={80} {...register('props.gapX', { valueAsNumber: true })} /></label>
        <label>Gap Y<input type="number" min={0} max={80} {...register('props.gapY', { valueAsNumber: true })} /></label>
      </div>
      <div className="props-grid-2">
        <label>Corner radius<input type="number" min={0} max={20} {...register('props.radius', { valueAsNumber: true })} /></label>
        <label>Sector gap<input type="number" min={0} max={6} {...register('props.sectorGap', { valueAsNumber: true })} /></label>
      </div>
      <label>Track color<input type="color" {...register('props.trackColor')} /></label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.showValue')} /> Show average</label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.showLabel')} /> Show corner labels</label>
    </section>
  )
}

// Graph: min/max/windowSec/lineWidth/fill — через RHF register
//        signals — direct onUpdate (обходим flatten/unflatten для массивов)
function GraphFields({
  register,
  widget,
  onUpdate,
}: { register: any; widget: Widget; onUpdate: (w: Widget) => void }) {
  const w = widget as any
  const signals: Array<{ signal: string; color: string }> = w.props?.signals ?? []

  function updateSignals(newSignals: typeof signals) {
    const newW = JSON.parse(JSON.stringify(w))
    newW.props.signals = newSignals
    onUpdate(newW as Widget)
  }

  return (
    <section>
      <div className="props-section-title">Graph</div>
      <div className="props-grid-2">
        <label>Min<input type="number" {...register('props.min', { valueAsNumber: true })} /></label>
        <label>Max<input type="number" {...register('props.max', { valueAsNumber: true })} /></label>
      </div>
      <label>Window (sec)<input type="number" min={5} max={120} {...register('props.windowSec', { valueAsNumber: true })} /></label>
      <label>Line width<input type="number" min={1} max={6} {...register('props.lineWidth', { valueAsNumber: true })} /></label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.fill')} /> Fill area</label>

      <div className="props-section-title" style={{ marginTop: '10px' }}>Signals</div>
      {signals.map((sig, i) => (
        <div key={i} style={{ display: 'flex', gap: '4px', alignItems: 'center', marginBottom: '4px' }}>
          <select
            value={sig.signal}
            style={{ flex: 1, fontSize: '11px', minWidth: 0 }}
            onChange={e => updateSignals(signals.map((s, j) => j === i ? { ...s, signal: e.target.value } : s))}
          >
            {SIGNALS.map(s => <option key={s} value={s}>{s}</option>)}
          </select>
          <input
            type="color"
            value={sig.color}
            style={{ width: '30px', height: '22px', padding: 0, border: 'none', borderRadius: '3px', cursor: 'pointer', flexShrink: 0 }}
            onChange={e => updateSignals(signals.map((s, j) => j === i ? { ...s, color: e.target.value } : s))}
          />
          <button
            onClick={() => updateSignals(signals.filter((_, j) => j !== i))}
            style={{ background: 'none', border: '1px solid #555', color: '#888', borderRadius: '3px', width: '20px', height: '22px', cursor: 'pointer', fontSize: '10px', flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}
          >✕</button>
        </div>
      ))}
      <button
        onClick={() => updateSignals([...signals, { signal: 'engine.rpm', color: '#FF9F0A' }])}
        style={{ fontSize: '11px', padding: '3px 8px', marginTop: '2px', width: '100%' }}
      >+ Add signal</button>
    </section>
  )
}

function ClockFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Clock</div>
      <label>Format
        <select {...register('props.format')}>
          <option value="24h">24h</option>
          <option value="12h">12h</option>
        </select>
      </label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.showSeconds')} /> Show seconds</label>
      <label>Color<input type="color" {...register('props.color')} /></label>
    </section>
  )
}

// ─── Flatten / Unflatten ──────────────────────────────────────────────────────

function flattenWidget(w: Widget): Record<string, unknown> {
  return {
    id:       w.id,
    type:     w.type,
    'rect.x': w.rect.x,
    'rect.y': w.rect.y,
    'rect.w': w.rect.w,
    'rect.h': w.rect.h,
    z:        w.z ?? 0,
    signal:   (w as any).signal ?? '',
    unit:     (w as any).unit ?? '',
    ...flattenProps((w as any).props ?? {}),
  }
}

function flattenProps(props: Record<string, unknown>, prefix = 'props'): Record<string, unknown> {
  const out: Record<string, unknown> = {}
  for (const [k, v] of Object.entries(props)) {
    if (typeof v === 'object' && v !== null && !Array.isArray(v)) {
      Object.assign(out, flattenProps(v as Record<string, unknown>, `${prefix}.${k}`))
    } else {
      out[`${prefix}.${k}`] = v
    }
  }
  return out
}

/**
 * Читает значение по пути с учётом ОБОИХ форматов RHF.
 *
 * react-hook-form хранит _formValues одновременно в двух видах:
 *   • плоско  — { 'props.min': 0 }   (клон defaultValues)
 *   • вложенно — { props: { min: 0 } } (создаётся register() через set())
 * Приоритет отдаём вложенному: он обновляется при вводе, плоский остаётся исходным.
 */
function readValue(values: Record<string, unknown>, path: string): unknown {
  const parts = path.split('.')
  let cur: unknown = values
  for (const p of parts) {
    if (cur === null || typeof cur !== 'object') { cur = undefined; break }
    cur = (cur as Record<string, unknown>)[p]
  }
  if (cur !== undefined) return cur
  return values[path]   // fallback на плоский ключ
}

function setPath(obj: Record<string, unknown>, path: string, val: unknown) {
  const parts = path.split('.')
  let cur = obj
  for (let i = 0; i < parts.length - 1; i++) {
    if (cur[parts[i]] === undefined || typeof cur[parts[i]] !== 'object' || cur[parts[i]] === null) {
      cur[parts[i]] = {}
    }
    cur = cur[parts[i]] as Record<string, unknown>
  }
  cur[parts[parts.length - 1]] = val
}

function unflattenWidget(original: Widget, values: Record<string, unknown>): Widget | null {
  try {
    // Deep clone — все оригинальные пропы (zones, ticks, stages, signals…) сохранены
    const w = JSON.parse(JSON.stringify(original)) as Record<string, unknown>

    // Множество путей-кандидатов: из оригинала + всё что реально есть в форме
    const keys = new Set(Object.keys(flattenWidget(original)))
    for (const k of Object.keys(values)) if (k.includes('.')) keys.add(k)
    const nestedProps = values['props']
    if (nestedProps && typeof nestedProps === 'object' && !Array.isArray(nestedProps)) {
      for (const k of Object.keys(nestedProps as object)) keys.add(`props.${k}`)
    }

    for (const key of keys) {
      // id/type неизменны
      if (key === 'id' || key === 'type') continue
      // rect и z принадлежат канве (drag/resize) — форма их не диктует.
      // Иначе отстающие значения формы откатывают виджет при перетаскивании.
      if (key.startsWith('rect.') || key === 'z') continue
      // props.signals — массив, редактируется напрямую в GraphFields через onUpdate
      if (key === 'props.signals') continue

      const val = readValue(values, key)
      if (val === undefined) continue
      // Никогда не присваиваем объекты/массивы целиком — только скаляры
      if (typeof val === 'object' && val !== null) continue
      if (typeof val === 'number' && isNaN(val)) continue     // пустое number-поле
      if (val === '' && key !== 'signal' && key !== 'unit') continue

      setPath(w, key, val)
    }

    return w as unknown as Widget
  } catch {
    return null
  }
}
