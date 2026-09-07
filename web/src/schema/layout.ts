/**
 * TypeScript-типы схемы лейаута.
 * Синхронизированы с docs/schema/layout.schema.json — при добавлении
 * нового виджета правь оба места.
 */

export type Color = `#${string}` // '#RRGGBB'
export type SignalId = string     // 'engine.rpm', 'sensor.oil_p', ...
export type SlugId = string

// ─── Геометрия ────────────────────────────────────────────────────────────────

export interface Rect {
  x: number
  y: number
  w: number
  h: number
}

// ─── Тема ─────────────────────────────────────────────────────────────────────

export interface Theme {
  bg: Color
  fg: Color
  accent: Color
  muted: Color
  warn: Color
  crit: Color
  fonts?: Record<string, string>
}

export const DEFAULT_THEME: Theme = {
  bg: '#000000',
  fg: '#FFFFFF',
  accent: '#FF3B30',
  muted: '#6E6E73',
  warn: '#FFCC00',
  crit: '#FF3B30',
}

// ─── Зоны шкалы ───────────────────────────────────────────────────────────────

export interface ScaleZone {
  from: number
  to: number
  color: Color
}

export interface ScaleTicks {
  major: number
  minor?: number
  labels?: boolean
  labelDivisor?: number
}

// ─── Виджеты ──────────────────────────────────────────────────────────────────

export type WidgetType =
  | 'needle_gauge'
  | 'bar'
  | 'numeric'
  | 'label'
  | 'shift_light'
  | 'warning'
  | 'graph'
  | 'gforce'
  | 'steering'
  | 'image'
  | 'clock'
  | 'lap_timer'

interface WidgetBase {
  id: SlugId
  type: WidgetType
  rect: Rect
  z?: number
  signal?: SignalId
  unit?: string
}

// numeric — цифровое значение
export interface NumericProps {
  decimals?: number
  font?: string
  fontSize?: number      // px; 0/undefined = автоподгонка под rect
  align?: 'left' | 'center' | 'right'
  color?: Color
  showUnit?: boolean
  caption?: string
  colorFromZones?: boolean
  zones?: ScaleZone[]
  staleAfterMs?: number
}
export interface NumericWidget extends WidgetBase { type: 'numeric'; props: NumericProps }

// label — статичный текст
export interface LabelProps {
  text: string
  font?: string
  fontSize?: number      // px; 0/undefined = автоподгонка под rect
  align?: 'left' | 'center' | 'right'
  color?: Color
}
export interface LabelWidget extends WidgetBase { type: 'label'; props: LabelProps }

// bar — линейная полоса
export interface BarProps {
  min: number
  max: number
  orientation?: 'horizontal' | 'vertical'
  segments?: number
  radius?: number
  color?: Color
  trackColor?: Color
  zones?: ScaleZone[]
}
export interface BarWidget extends WidgetBase { type: 'bar'; props: BarProps }

// shift_light
export interface ShiftStage {
  at: number
  color: Color
  blinkHz?: number
}
export interface ShiftLightProps {
  mode?: 'segments' | 'arc' | 'flash'
  stages: ShiftStage[]
  dotSize?: number    // px; 0 = auto (based on rect size)
}
export interface ShiftLightWidget extends WidgetBase { type: 'shift_light'; props: ShiftLightProps }

// warning
export type WarningIcon = 'oil' | 'temp' | 'battery' | 'check_engine' | 'fuel' | 'brake' | 'generic'
export interface WarningProps {
  icon: WarningIcon
  color?: Color
  blinkHz?: number
  label?: string        // "LOW OIL", "HIGH TEMP" — подпись
  triggerBelow?: number // показывать если signal < значения
  triggerAbove?: number // показывать если signal > значения
}
export interface WarningWidget extends WidgetBase { type: 'warning'; props: WarningProps }

// graph
export interface GraphSignal { signal: SignalId; color: Color }
export interface GraphProps {
  min: number
  max: number
  windowSec?: number
  lineWidth?: number
  fill?: boolean
  signals: GraphSignal[]
}
export interface GraphWidget extends WidgetBase { type: 'graph'; props: GraphProps }

// gforce
export interface GForceProps {
  range?: number
  rings?: number
  trail?: boolean
  signalX?: SignalId
  signalY?: SignalId
}
export interface GForceWidget extends WidgetBase { type: 'gforce'; props: GForceProps }

// steering — положение руля: полоса, растущая из центра в сторону поворота
export interface SteeringProps {
  color?: Color
  trackColor?: Color
  radius?: number
  centerMark?: boolean     // риска в нуле; default true
  showValue?: boolean      // цифры в градусах под полосой
  maxAngle?: number        // градусы при полном отклонении; для цифр
  deadzone?: number        // 0..1 — зона нечувствительности вокруг центра
}
export interface SteeringWidget extends WidgetBase { type: 'steering'; props: SteeringProps }

// needle_gauge — круглый циферблат со стрелкой
export interface NeedleGaugeProps {
  min: number
  max: number
  startAngle?: number
  endAngle?: number
  needleColor?: Color
  needleWidth?: number
  hubRadius?: number
  zones?: ScaleZone[]
  ticks?: ScaleTicks
}
export interface NeedleGaugeWidget extends WidgetBase { type: 'needle_gauge'; props: NeedleGaugeProps }

// image
export interface ImageProps { src: string; opacity?: number }
export interface ImageWidget extends WidgetBase { type: 'image'; props: ImageProps }

// clock — время из RTC PCF8563
export interface ClockProps {
  format?: '12h' | '24h'   // default '24h'
  showSeconds?: boolean
  color?: Color
  font?: string
}
export interface ClockWidget extends WidgetBase { type: 'clock'; props: ClockProps }

// lap_timer — таймер круга (активируется по сигналу GPS/магнита)
export interface LapTimerProps {
  color?: Color
  bestColor?: Color       // цвет когда лучший результат (обычно зелёный)
  showBest?: boolean
  showDelta?: boolean
}
export interface LapTimerWidget extends WidgetBase { type: 'lap_timer'; props: LapTimerProps }

export type Widget =
  | NeedleGaugeWidget
  | NumericWidget
  | LabelWidget
  | BarWidget
  | ShiftLightWidget
  | WarningWidget
  | GraphWidget
  | GForceWidget
  | SteeringWidget
  | ImageWidget
  | ClockWidget
  | LapTimerWidget

// ─── Экраны ───────────────────────────────────────────────────────────────────

export interface Screen {
  id: SlugId
  name?: string
  bg?: Color
  widgets: Widget[]
}

// ─── Лейаут ───────────────────────────────────────────────────────────────────

export interface Display {
  w: number
  h: number
  shape: 'round' | 'rect'
}

export const DISPLAY_466: Display = { w: 466, h: 466, shape: 'round' }

/**
 * Симуляция сигналов на устройстве.
 *
 * Флаг едет внутри лейаута, а не в настройках прошивки: так один и тот же
 * файл, проверенный в редакторе, ведёт себя на устройстве точно так же —
 * достаточно залить его в LittleFS.
 */
export interface SimConfig {
  enabled: boolean
}

export interface Layout {
  schema: 1
  id: SlugId
  name: string
  author?: string
  car?: string
  display: Display
  theme: Theme
  sim?: SimConfig
  screens: Screen[]
}

// ─── Вспомогательные ──────────────────────────────────────────────────────────

/** Разрешить цвет зоны для значения val, иначе fallback */
export function zoneColor(zones: ScaleZone[] | undefined, val: number, fallback: Color): Color {
  if (!zones) return fallback
  for (const z of zones) {
    if (val >= z.from && val <= z.to) return z.color
  }
  return fallback
}

/** Нормализовать значение в [0,1] по диапазону min..max */
export function norm(val: number, min: number, max: number): number {
  return Math.max(0, Math.min(1, (val - min) / (max - min)))
}

/** Градусы → радианы (наш 0° = вверх, по часовой) */
export function degToRad(deg: number): number {
  // CSS/canvas: 0° = вправо, -90° = вверх
  return ((deg - 90) * Math.PI) / 180
}
