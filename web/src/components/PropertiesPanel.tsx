/**
 * PropertiesPanel â€” Ð¿Ñ€Ð°Ð²Ð°Ñ Ð¿Ð°Ð½ÐµÐ»ÑŒ Ð¸Ð½ÑÐ¿ÐµÐºÑ‚Ð¾Ñ€Ð°.
 *
 * Ð˜Ð¡ÐŸÐ ÐÐ’Ð›Ð•ÐÐ˜Ð¯:
 *  1. Drag/resize ÑÐ±Ñ€Ð¾Ñ: widgetRef (Ð°ÐºÑ‚ÑƒÐ°Ð»ÑŒÐ½Ñ‹Ð¹ Ð¿Ð¾ÑÐ»Ðµ ÐºÐ°Ð¶Ð´Ð¾Ð³Ð¾ Ñ€ÐµÐ½Ð´ÐµÑ€Ð°) +
 *     JSON-ÑÑ€Ð°Ð²Ð½ÐµÐ½Ð¸Ðµ Ñ€ÐµÐ·ÑƒÐ»ÑŒÑ‚Ð°Ñ‚Ð° Ñ Ñ‚ÐµÐºÑƒÑ‰Ð¸Ð¼ Ð²Ð¸Ð´Ð¶ÐµÑ‚Ð¾Ð¼ Ð¿ÐµÑ€ÐµÐ´ onUpdate
 *  2. Shift Light: ÑÐºÑ€Ñ‹Ñ‚Ð° ÑÐµÐºÑ†Ð¸Ñ Signal Ð¸ Mode; dotSize Ñ‚Ð¾Ð»ÑŒÐºÐ¾ Ð´Ð»Ñ arc
 *  3. Warning: ÑƒÐ±Ñ€Ð°Ð½ blinkHz Ð¸Ð· Ð¿Ð°Ð½ÐµÐ»Ð¸
 *  4. Graph: Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¾Ñ€ ÑÐ¸Ð³Ð½Ð°Ð»Ð¾Ð² Ñ direct-onUpdate; props.signals ÑÐºÐ¸Ð¿Ð°ÐµÑ‚ÑÑ Ð² unflattenWidget
 *  5. Bar orientation: Ð°Ð²Ñ‚Ð¾-ÑÐ²Ð¾Ð¿ wâ†”h
 */

import { useEffect, useRef } from 'react'
import { useForm, useWatch } from 'react-hook-form'
import { useEditorStore, useSelectedWidget } from '@/store'
import { EM_LADDER_BOLD } from '@/renderer/deviceFont'
import { Widget } from '@/schema/layout'

const SIGNALS = [
  'engine.rpm', 'engine.coolant_t', 'engine.load',
  'engine.map', 'engine.timing', 'engine.iat',
  'veh.speed',
  'sensor.oil_p', 'sensor.oil_t', 'sensor.egt',
  'sys.battery',
  'imu.ax', 'imu.ay',
  'pedal.brake', 'pedal.accel', 'steer.pos',
  'calc.oil_p_margin',
]

// â”€â”€â”€ ÐšÐ¾Ñ€Ð½ÐµÐ²Ð¾Ð¹ ÐºÐ¾Ð¼Ð¿Ð¾Ð½ÐµÐ½Ñ‚ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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
                  ? 'ÐŸÐµÑ€ÐµÐ¾Ð¿Ñ€ÐµÐ´ÐµÐ»ÑÐµÑ‚ Ñ„Ð¾Ð½ Ñ‚ÐµÐ¼Ñ‹ Ð´Ð»Ñ ÑÑ‚Ð¾Ð³Ð¾ ÑÐºÑ€Ð°Ð½Ð°'
                  : `ÐÐ°ÑÐ»ÐµÐ´ÑƒÐµÑ‚ Ñ‚ÐµÐ¼Ñƒ (${layout.theme.bg})`}
              </div>
              {screen.bg && (
                <button
                  type="button"
                  onClick={() => setScreenBg(activeScreenIdx, layout.theme.bg)}
                  style={{ fontSize: '11px', padding: '3px 8px', marginTop: '4px' }}
                >Ð¡Ð±Ñ€Ð¾ÑÐ¸Ñ‚ÑŒ Ðº Ñ‚ÐµÐ¼Ðµ</button>
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
        >âœ•</button>
      </div>
      <WidgetForm
        key={selected.id}
        widget={selected}
        onUpdate={w => updateWidget(activeScreenIdx, w)}
      />
    </div>
  )
}

// â”€â”€â”€ Ð¤Ð¾Ñ€Ð¼Ð° â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

interface FormProps { widget: Widget; onUpdate: (w: Widget) => void }

function WidgetForm({ widget, onUpdate }: FormProps) {
  const { register, control, reset, setValue } = useForm({
    defaultValues: flattenWidget(widget),
  })

  // ÐŸÐ¾Ð»Ð½Ñ‹Ð¹ ÑÐ±Ñ€Ð¾Ñ Ñ‚Ð¾Ð»ÑŒÐºÐ¾ Ð¿Ñ€Ð¸ ÑÐ¼ÐµÐ½Ðµ Ð²Ð¸Ð´Ð¶ÐµÑ‚Ð° (Ð´Ñ€ÑƒÐ³Ð¾Ð¹ id)
  useEffect(() => { reset(flattenWidget(widget)) }, [widget.id]) // eslint-disable-line

  // â”€â”€ Ð’ÑÐµÐ³Ð´Ð° Ð°ÐºÑ‚ÑƒÐ°Ð»ÑŒÐ½Ð°Ñ ÑÑÑ‹Ð»ÐºÐ° Ð½Ð° Ð²Ð¸Ð´Ð¶ÐµÑ‚ Ð¸Ð· ÑÑ‚Ð¾Ñ€Ð° â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Ð’ÐÐ–ÐÐž: Ð¾Ð±ÑŠÑÐ²Ð»ÐµÐ½Ð° ÐŸÐ•Ð Ð•Ð” Ð²ÑÐµÐ¼Ð¸ useEffect(, [values]) Ñ‡Ñ‚Ð¾Ð±Ñ‹ Ð¾Ð±Ð½Ð¾Ð²Ð»ÑÑ‚ÑŒÑÑ Ñ€Ð°Ð½ÑŒÑˆÐµ!
  const widgetRef = useRef(widget)
  useEffect(() => { widgetRef.current = widget })  // no deps â†’ ÐºÐ°Ð¶Ð´Ñ‹Ð¹ Ñ€ÐµÐ½Ð´ÐµÑ€

  // â”€â”€ Ð¡Ð¸Ð½Ñ…Ñ€Ð¾Ð½Ð¸Ð·Ð°Ñ†Ð¸Ñ rect/z Ð¸Ð· canvas drag/resize â†’ Ð² Ñ„Ð¾Ñ€Ð¼Ñƒ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

  // â”€â”€ Live-Ð¾Ð±Ð½Ð¾Ð²Ð»ÐµÐ½Ð¸Ðµ Ñ Ð´Ð²Ð¾Ð¹Ð½Ð¾Ð¹ Ð·Ð°Ñ‰Ð¸Ñ‚Ð¾Ð¹ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  1. JSON-ÑÑ€Ð°Ð²Ð½ÐµÐ½Ð¸Ðµ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ð¹ Ñ„Ð¾Ñ€Ð¼Ñ‹ â†’ Ð½Ðµ Ð¿ÐµÑ€ÐµÐ·Ð°Ð¿ÑƒÑÐºÐ°ÐµÐ¼ ÐµÑÐ»Ð¸ Ñ„Ð¾Ñ€Ð¼Ð° Ð½Ðµ Ð¸Ð·Ð¼ÐµÐ½Ð¸Ð»Ð°ÑÑŒ
  //  2. JSON-ÑÑ€Ð°Ð²Ð½ÐµÐ½Ð¸Ðµ Ñ€ÐµÐ·ÑƒÐ»ÑŒÑ‚Ð°Ñ‚Ð° Ñ Ð°ÐºÑ‚ÑƒÐ°Ð»ÑŒÐ½Ñ‹Ð¼ Ð²Ð¸Ð´Ð¶ÐµÑ‚Ð¾Ð¼ â†’ drag Ð½Ðµ ÑÐ±Ñ€Ð°ÑÑ‹Ð²Ð°ÐµÑ‚ÑÑ!
  //     (stale form values, rect.x=110, Ð¿Ð¾ÐºÐ° canvas ÑƒÐ¶Ðµ Ð½Ð° x=120 â†’ SKIP)
  const values = useWatch({ control })
  const lastJsonRef = useRef<string>('')

  useEffect(() => {
    const json = JSON.stringify(values)
    if (json === lastJsonRef.current) return
    lastJsonRef.current = json

    // widgetRef.current Ð¾Ð±Ð½Ð¾Ð²Ð»Ñ‘Ð½ Ð½Ð° ÑÑ‚Ð¾Ð¼ Ð¶Ðµ Ñ€ÐµÐ½Ð´ÐµÑ€Ðµ Ð² ÑÑ„Ñ„ÐµÐºÑ‚Ðµ Ð²Ñ‹ÑˆÐµ
    const current = widgetRef.current
    const result  = unflattenWidget(current, values as Record<string, unknown>)
    if (!result) return

    // Bar: ÑÐ¼ÐµÐ½Ð° orientation Ð¼ÐµÐ½ÑÐµÑ‚ Ð¼ÐµÑÑ‚Ð°Ð¼Ð¸ wâ†”h. Ð”ÐµÐ»Ð°ÐµÐ¼ Ð² Ð¾Ð´Ð½Ð¾Ð¼ Ð°Ð¿Ð´ÐµÐ¹Ñ‚Ðµ Ñ
    // ÑÐ°Ð¼Ð¾Ð¹ Ð¾Ñ€Ð¸ÐµÐ½Ñ‚Ð°Ñ†Ð¸ÐµÐ¹, Ð¸Ð½Ð°Ñ‡Ðµ Ð²Ñ‚Ð¾Ñ€Ð¾Ð¹ onUpdate Ð¿Ñ€Ð¾Ñ‡Ð¸Ñ‚Ð°ÐµÑ‚ ÑƒÑÑ‚Ð°Ñ€ÐµÐ²ÑˆÐ¸Ð¹ widgetRef.
    const prevOrient = (current as any).props?.orientation
    const nextOrient = (result  as any).props?.orientation
    if (current.type === 'bar' && prevOrient && nextOrient && prevOrient !== nextOrient) {
      result.rect = { ...result.rect, w: current.rect.h, h: current.rect.w }
    }

    // ÐÐ¸Ñ‡ÐµÐ³Ð¾ Ð½Ðµ Ð¸Ð·Ð¼ÐµÐ½Ð¸Ð»Ð¾ÑÑŒ â€” Ð½Ðµ Ð¿Ð»Ð¾Ð´Ð¸Ð¼ Ð·Ð°Ð¿Ð¸ÑÐ¸ Ð² Ð¸ÑÑ‚Ð¾Ñ€Ð¸Ð¸
    if (JSON.stringify(result) === JSON.stringify(current)) return

    onUpdate(result)
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [values])

  // â”€â”€ Ð ÑƒÑ‡Ð½Ð¾Ð¹ Ð²Ð²Ð¾Ð´ rect/z: ÐºÐ¾Ð¼Ð¼Ð¸Ñ‚ Ð¿Ð¾ blur Ð¸Ð»Ð¸ Enter â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Ð–Ð¸Ð²Ð¾Ð¹ Ð¿ÑƒÑ‚ÑŒ Ñ„Ð¾Ñ€Ð¼Ñ‹ rect Ð½Ðµ Ñ‚Ñ€Ð¾Ð³Ð°ÐµÑ‚ (ÑÐ¼. unflattenWidget), Ð¿Ð¾ÑÑ‚Ð¾Ð¼Ñƒ Ð¿Ñ€Ð¸Ð¼ÐµÐ½ÑÐµÐ¼ ÑÐ²Ð½Ð¾.
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

  // ÐžÐ±Ð¾Ñ€Ð°Ñ‡Ð¸Ð²Ð°ÐµÐ¼ register Ñ‡Ñ‚Ð¾Ð±Ñ‹ ÑÐ¾Ñ…Ñ€Ð°Ð½Ð¸Ñ‚ÑŒ onBlur ÑÐ°Ð¼Ð¾Ð³Ð¾ RHF Ð¸ Ð´Ð¾Ð±Ð°Ð²Ð¸Ñ‚ÑŒ ÑÐ²Ð¾Ð¹ ÐºÐ¾Ð¼Ð¼Ð¸Ñ‚
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

  // Shift light â€” signal Ð²ÑÐµÐ³Ð´Ð° engine.rpm, Ð¼ÐµÐ½ÑÑ‚ÑŒ Ð½Ðµ Ð½ÑƒÐ¶Ð½Ð¾
  const hideSignal = ['label', 'image', 'gforce', 'graph', 'shift_light'].includes(widget.type)

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
        <div className="props-hint">Enter / ÐºÐ»Ð¸Ðº Ð²Ð½Ðµ Ð¿Ð¾Ð»Ñ â€” Ð¿Ñ€Ð¸Ð¼ÐµÐ½Ð¸Ñ‚ÑŒ</div>
      </section>

      {/* Signal / Data */}
      {!hideSignal && (
        <section>
          <div className="props-section-title">Data</div>
          <label>Signal
            <select {...register('signal')}>
              <option value="">â€” none â€”</option>
              {SIGNALS.map(s => <option key={s} value={s}>{s}</option>)}
            </select>
          </label>
          <label>Unit <input {...register('unit')} placeholder="km/h, Â°C, barâ€¦" /></label>
        </section>
      )}

      {/* Type-specific ÑÐµÐºÑ†Ð¸Ð¸ */}
      {widget.type === 'arc_gauge'   && <ArcGaugeFields register={register} />}
      {widget.type === 'numeric'     && <NumericFields register={register} />}
      {widget.type === 'label'       && <LabelFields register={register} />}
      {widget.type === 'bar'         && <BarFields register={register} />}
      {widget.type === 'shift_light' && <ShiftLightFields register={register} widget={widget} />}
      {widget.type === 'warning'     && <WarningFields register={register} />}
      {widget.type === 'gforce'      && <GForceFields register={register} />}
      {widget.type === 'steering'    && <SteeringFields register={register} />}
      {widget.type === 'graph'       && <GraphFields register={register} widget={widget} onUpdate={onUpdate} />}
      {widget.type === 'clock'       && <ClockFields register={register} />}
    </form>
  )
}

// â”€â”€â”€ Ð¡ÐµÐºÑ†Ð¸Ð¸ Ð¿Ð¾Ð»ÐµÐ¹ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

function ArcGaugeFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Arc Gauge</div>
      <div className="props-grid-2">
        <label>Min<input type="number" {...register('props.min', { valueAsNumber: true })} /></label>
        <label>Max<input type="number" {...register('props.max', { valueAsNumber: true })} /></label>
        <label>StartÂ°<input type="number" {...register('props.startAngle', { valueAsNumber: true })} /></label>
        <label>EndÂ°<input type="number" {...register('props.endAngle', { valueAsNumber: true })} /></label>
        <label>Thickness<input type="number" {...register('props.thickness', { valueAsNumber: true })} /></label>
      </div>
      <label className="checkbox-row"><input type="checkbox" {...register('props.rounded')} /> Rounded caps</label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.peakHold')} /> Peak hold</label>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <label>Track color<input type="color" {...register('props.trackColor')} /></label>
    </section>
  )
}

function NumericFields({ register }: { register: any }) {
  return (
    <section>
      <div className="props-section-title">Numeric</div>
      <label>Decimals<input type="number" min={0} max={3} {...register('props.decimals', { valueAsNumber: true })} /></label>
      <FontSizeField register={register} />
      <label>Caption<input {...register('props.caption')} placeholder="OIL, COOLANTâ€¦" /></label>
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
      <div className="props-hint">Wâ†”H auto-swapped on orientation change</div>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <label>Track color<input type="color" {...register('props.trackColor')} /></label>
    </section>
  )
}

// Mode ÑÐºÑ€Ñ‹Ñ‚ â€” Ð¾Ð¿Ñ€ÐµÐ´ÐµÐ»Ñ‘Ð½ Ð¿Ð°Ð»Ð¸Ñ‚Ñ€Ð¾Ð¹. dotSize Ñ‚Ð¾Ð»ÑŒÐºÐ¾ Ð´Ð»Ñ arc.
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
        <div className="props-hint">Rect 0,0,466,466 Ð´Ð»Ñ full-screen</div>
      )}
      <div className="props-hint">Stages (at, color, blinkHz) â€” JSON export</div>
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
      <label>Label<input {...register('props.label')} placeholder="LOW OIL, HIGH TEMPâ€¦" /></label>
      <label>Trigger below<input type="number" step="0.1" {...register('props.triggerBelow', { valueAsNumber: true })} placeholder="e.g. 1.5" /></label>
      <label>Trigger above<input type="number" step="0.1" {...register('props.triggerAbove', { valueAsNumber: true })} placeholder="e.g. 110" /></label>
      <label>Color<input type="color" {...register('props.color')} /></label>
      <div className="props-hint">
        ÐŸÐ¾ÑÐ²Ð»ÑÐµÑ‚ÑÑ Ð½Ð° 2 Ñ Ð¿Ñ€Ð¸ ÑÑ€Ð°Ð±Ð°Ñ‚Ñ‹Ð²Ð°Ð½Ð¸Ð¸ Ñ‚Ñ€Ð¸Ð³Ð³ÐµÑ€Ð°.<br />
        Ð¢Ñ€ÐµÐ±ÑƒÐµÑ‚ Signal + Trigger below/above.
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
      <label className="checkbox-row"><input type="checkbox" {...register('props.trail')} /> Show trail</label>
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

/**
 * ÐšÐµÐ³Ð»ÑŒ Ð´Ð»Ñ Label Ð¸ Numeric.
 *
 * Ð¡Ð¿Ð¸ÑÐ¾Ðº Ð´Ð¾ÑÑ‚Ð¸Ð¶Ð¸Ð¼Ñ‹Ñ… Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ð¹ Ð¿Ð¾Ð´ÑÐºÐ°Ð·Ñ‹Ð²Ð°ÐµÑ‚ÑÑ Ñ‡ÐµÑ€ÐµÐ· datalist: Ð¿Ñ€Ð¾ÑˆÐ¸Ð²ÐºÐ° Ñ€Ð¸ÑÑƒÐµÑ‚
 * Ð±Ð¸Ñ‚Ð¼Ð°Ð¿Ð½Ñ‹Ð¼Ð¸ ÑˆÑ€Ð¸Ñ„Ñ‚Ð°Ð¼Ð¸ Ð¸ ÑƒÐ¼ÐµÐµÑ‚ Ñ‚Ð¾Ð»ÑŒÐºÐ¾ ÐºÐ¾Ð½ÐµÑ‡Ð½Ñ‹Ð¹ Ð½Ð°Ð±Ð¾Ñ€ Ð²Ñ‹ÑÐ¾Ñ‚, Ð° Ð¿Ñ€Ð¾Ð¼ÐµÐ¶ÑƒÑ‚Ð¾Ñ‡Ð½Ñ‹Ðµ
 * Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ñ Ð¾Ð½Ð° Ð¿Ñ€Ð¸Ð¶Ð¼Ñ‘Ñ‚ Ðº Ð±Ð»Ð¸Ð¶Ð°Ð¹ÑˆÐµÐ¹ ÑÑ‚ÑƒÐ¿ÐµÐ½Ð¸. Ð’Ð²Ð¾Ð´ Ð½Ðµ Ð·Ð°Ð¿Ñ€ÐµÑ‰Ñ‘Ð½ â€” Ð¿Ñ€Ð¾ÑÑ‚Ð¾ Ð²Ð¸Ð´Ð½Ð¾,
 * Ñ‡Ñ‚Ð¾ Ñ€ÐµÐ°Ð»ÑŒÐ½Ð¾ Ð¿Ð¾Ð»ÑƒÑ‡Ð¸Ñ‚ÑÑ Ð½Ð° ÑƒÑÑ‚Ñ€Ð¾Ð¹ÑÑ‚Ð²Ðµ.
 */
function FontSizeField({ register }: { register: any }) {
  return (
    <>
      <label>Font size
        <input
          type="number" min={0} max={200} list="device-font-sizes"
          {...register('props.fontSize', { valueAsNumber: true })}
        />
      </label>
      <datalist id="device-font-sizes">
        {EM_LADDER_BOLD.map(v => <option key={v} value={v} />)}
      </datalist>
      <div className="props-hint">
        0 = Ð°Ð²Ñ‚Ð¾: Ñ‚ÐµÐºÑÑ‚ Ñ€Ð°ÑÑ‚Ñ‘Ñ‚ Ð²ÑÐ»ÐµÐ´ Ð·Ð° Ñ€Ð°Ð¼ÐºÐ¾Ð¹. Ð£ÑÑ‚Ñ€Ð¾Ð¹ÑÑ‚Ð²Ð¾ ÑƒÐ¼ÐµÐµÑ‚ Ñ‚Ð¾Ð»ÑŒÐºÐ¾
        ÐºÐµÐ³Ð»Ð¸ {EM_LADDER_BOLD.slice(0, 6).join(', ')}â€¦ â€” Ð¾ÑÑ‚Ð°Ð»ÑŒÐ½Ñ‹Ðµ Ð¿Ñ€Ð¸Ð¶Ð¼ÑƒÑ‚ÑÑ
        Ðº Ð±Ð»Ð¸Ð¶Ð°Ð¹ÑˆÐµÐ¼Ñƒ.
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
      <label>Max angle (Â°)<input type="number" min={90} max={1080} step={10} {...register('props.maxAngle', { valueAsNumber: true })} /></label>
      <label>Deadzone<input type="number" min={0} max={0.5} step={0.01} {...register('props.deadzone', { valueAsNumber: true })} /></label>
      <div className="props-hint">Ð—Ð¾Ð½Ð° Ð½ÐµÑ‡ÑƒÐ²ÑÑ‚Ð²Ð¸Ñ‚ÐµÐ»ÑŒÐ½Ð¾ÑÑ‚Ð¸ Ð²Ð¾ÐºÑ€ÑƒÐ³ Ñ†ÐµÐ½Ñ‚Ñ€Ð°, 0â€¦0.5</div>
      <label className="checkbox-row"><input type="checkbox" {...register('props.centerMark')} /> Center mark</label>
      <label className="checkbox-row"><input type="checkbox" {...register('props.showValue')} /> Show angle</label>
    </section>
  )
}

// Graph: min/max/windowSec/lineWidth/fill â€” Ñ‡ÐµÑ€ÐµÐ· RHF register
//        signals â€” direct onUpdate (Ð¾Ð±Ñ…Ð¾Ð´Ð¸Ð¼ flatten/unflatten Ð´Ð»Ñ Ð¼Ð°ÑÑÐ¸Ð²Ð¾Ð²)
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
          >âœ•</button>
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

// â”€â”€â”€ Flatten / Unflatten â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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
 * Ð§Ð¸Ñ‚Ð°ÐµÑ‚ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ Ð¿Ð¾ Ð¿ÑƒÑ‚Ð¸ Ñ ÑƒÑ‡Ñ‘Ñ‚Ð¾Ð¼ ÐžÐ‘ÐžÐ˜Ð¥ Ñ„Ð¾Ñ€Ð¼Ð°Ñ‚Ð¾Ð² RHF.
 *
 * react-hook-form Ñ…Ñ€Ð°Ð½Ð¸Ñ‚ _formValues Ð¾Ð´Ð½Ð¾Ð²Ñ€ÐµÐ¼ÐµÐ½Ð½Ð¾ Ð² Ð´Ð²ÑƒÑ… Ð²Ð¸Ð´Ð°Ñ…:
 *   â€¢ Ð¿Ð»Ð¾ÑÐºÐ¾  â€” { 'props.min': 0 }   (ÐºÐ»Ð¾Ð½ defaultValues)
 *   â€¢ Ð²Ð»Ð¾Ð¶ÐµÐ½Ð½Ð¾ â€” { props: { min: 0 } } (ÑÐ¾Ð·Ð´Ð°Ñ‘Ñ‚ÑÑ register() Ñ‡ÐµÑ€ÐµÐ· set())
 * ÐŸÑ€Ð¸Ð¾Ñ€Ð¸Ñ‚ÐµÑ‚ Ð¾Ñ‚Ð´Ð°Ñ‘Ð¼ Ð²Ð»Ð¾Ð¶ÐµÐ½Ð½Ð¾Ð¼Ñƒ: Ð¾Ð½ Ð¾Ð±Ð½Ð¾Ð²Ð»ÑÐµÑ‚ÑÑ Ð¿Ñ€Ð¸ Ð²Ð²Ð¾Ð´Ðµ, Ð¿Ð»Ð¾ÑÐºÐ¸Ð¹ Ð¾ÑÑ‚Ð°Ñ‘Ñ‚ÑÑ Ð¸ÑÑ…Ð¾Ð´Ð½Ñ‹Ð¼.
 */
function readValue(values: Record<string, unknown>, path: string): unknown {
  const parts = path.split('.')
  let cur: unknown = values
  for (const p of parts) {
    if (cur === null || typeof cur !== 'object') { cur = undefined; break }
    cur = (cur as Record<string, unknown>)[p]
  }
  if (cur !== undefined) return cur
  return values[path]   // fallback Ð½Ð° Ð¿Ð»Ð¾ÑÐºÐ¸Ð¹ ÐºÐ»ÑŽÑ‡
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
    // Deep clone â€” Ð²ÑÐµ Ð¾Ñ€Ð¸Ð³Ð¸Ð½Ð°Ð»ÑŒÐ½Ñ‹Ðµ Ð¿Ñ€Ð¾Ð¿Ñ‹ (zones, ticks, stages, signalsâ€¦) ÑÐ¾Ñ…Ñ€Ð°Ð½ÐµÐ½Ñ‹
    const w = JSON.parse(JSON.stringify(original)) as Record<string, unknown>

    // ÐœÐ½Ð¾Ð¶ÐµÑÑ‚Ð²Ð¾ Ð¿ÑƒÑ‚ÐµÐ¹-ÐºÐ°Ð½Ð´Ð¸Ð´Ð°Ñ‚Ð¾Ð²: Ð¸Ð· Ð¾Ñ€Ð¸Ð³Ð¸Ð½Ð°Ð»Ð° + Ð²ÑÑ‘ Ñ‡Ñ‚Ð¾ Ñ€ÐµÐ°Ð»ÑŒÐ½Ð¾ ÐµÑÑ‚ÑŒ Ð² Ñ„Ð¾Ñ€Ð¼Ðµ
    const keys = new Set(Object.keys(flattenWidget(original)))
    for (const k of Object.keys(values)) if (k.includes('.')) keys.add(k)
    const nestedProps = values['props']
    if (nestedProps && typeof nestedProps === 'object' && !Array.isArray(nestedProps)) {
      for (const k of Object.keys(nestedProps as object)) keys.add(`props.${k}`)
    }

    for (const key of keys) {
      // id/type Ð½ÐµÐ¸Ð·Ð¼ÐµÐ½Ð½Ñ‹
      if (key === 'id' || key === 'type') continue
      // rect Ð¸ z Ð¿Ñ€Ð¸Ð½Ð°Ð´Ð»ÐµÐ¶Ð°Ñ‚ ÐºÐ°Ð½Ð²Ðµ (drag/resize) â€” Ñ„Ð¾Ñ€Ð¼Ð° Ð¸Ñ… Ð½Ðµ Ð´Ð¸ÐºÑ‚ÑƒÐµÑ‚.
      // Ð˜Ð½Ð°Ñ‡Ðµ Ð¾Ñ‚ÑÑ‚Ð°ÑŽÑ‰Ð¸Ðµ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ñ Ñ„Ð¾Ñ€Ð¼Ñ‹ Ð¾Ñ‚ÐºÐ°Ñ‚Ñ‹Ð²Ð°ÑŽÑ‚ Ð²Ð¸Ð´Ð¶ÐµÑ‚ Ð¿Ñ€Ð¸ Ð¿ÐµÑ€ÐµÑ‚Ð°ÑÐºÐ¸Ð²Ð°Ð½Ð¸Ð¸.
      if (key.startsWith('rect.') || key === 'z') continue
      // props.signals â€” Ð¼Ð°ÑÑÐ¸Ð², Ñ€ÐµÐ´Ð°ÐºÑ‚Ð¸Ñ€ÑƒÐµÑ‚ÑÑ Ð½Ð°Ð¿Ñ€ÑÐ¼ÑƒÑŽ Ð² GraphFields Ñ‡ÐµÑ€ÐµÐ· onUpdate
      if (key === 'props.signals') continue

      const val = readValue(values, key)
      if (val === undefined) continue
      // ÐÐ¸ÐºÐ¾Ð³Ð´Ð° Ð½Ðµ Ð¿Ñ€Ð¸ÑÐ²Ð°Ð¸Ð²Ð°ÐµÐ¼ Ð¾Ð±ÑŠÐµÐºÑ‚Ñ‹/Ð¼Ð°ÑÑÐ¸Ð²Ñ‹ Ñ†ÐµÐ»Ð¸ÐºÐ¾Ð¼ â€” Ñ‚Ð¾Ð»ÑŒÐºÐ¾ ÑÐºÐ°Ð»ÑÑ€Ñ‹
      if (typeof val === 'object' && val !== null) continue
      if (typeof val === 'number' && isNaN(val)) continue     // Ð¿ÑƒÑÑ‚Ð¾Ðµ number-Ð¿Ð¾Ð»Ðµ
      if (val === '' && key !== 'signal' && key !== 'unit') continue

      setPath(w, key, val)
    }

    return w as unknown as Widget
  } catch {
    return null
  }
}
