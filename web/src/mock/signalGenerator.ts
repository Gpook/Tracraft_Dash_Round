/**
 * Генератор синтетических сигналов для предпросмотра редактора.
 * Все сигналы имитируют реальное поведение автомобиля на трек-сессии.
 */

export type SignalValues = Record<string, number>

// ─── Вспомогательные ──────────────────────────────────────────────────────────

function smoothstep(a: number, b: number, x: number): number {
  const t = Math.max(0, Math.min(1, (x - a) / (b - a)))
  return t * t * (3 - 2 * t)
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * Math.max(0, Math.min(1, t))
}

function noise(t: number, freq: number, amp: number): number {
  return amp * Math.sin(t * freq * 2 * Math.PI)
}

// ─── RPM: пилообразная форма — разгон до отсечки, 200мс зависание, сброс ─────

const RPM_IDLE   = 1200
const RPM_REDLINE = 7400   // чуть выше последнего порога 7200 → гарантирует мигание
const RPM_CYCLE  = 7.0     // секунд на один цикл
const RPM_HOLD   = 0.20    // секунд на удержание при отсечке
const RPM_DROP   = 0.35    // секунд на сброс газа

function computeRPM(t: number): number {
  const phase = t % RPM_CYCLE
  const riseEnd  = RPM_CYCLE - RPM_HOLD - RPM_DROP

  if (phase < riseEnd) {
    // Плавный разгон с подхватом в середине оборотов
    const f = phase / riseEnd
    const curved = f < 0.6
      ? smoothstep(0, 0.6, f) * 0.65          // набор в нижнем диапазоне
      : 0.65 + smoothstep(0.6, 1, f) * 0.35   // агрессивный подхват к отсечке
    return lerp(RPM_IDLE, RPM_REDLINE, curved)
  } else if (phase < riseEnd + RPM_HOLD) {
    // Удержание на отсечке (ограничитель) — с небольшим дрожанием
    return RPM_REDLINE + noise(t, 22, 60)
  } else {
    // Быстрый сброс и откат к холостым
    const dropF = (phase - riseEnd - RPM_HOLD) / RPM_DROP
    return lerp(RPM_REDLINE, RPM_IDLE + 400, smoothstep(0, 1, dropF))
  }
}

// ─── Педали ──────────────────────────────────────────────────────────────────

/// Пик торможения в конце цикла и он же — точка, с которой начинается отпуск
/// на выходе из поворота. Одно значение на оба конца делает кривую непрерывной
/// на стыке циклов.
const BRAKE_PEAK    = 0.9
const BRAKE_RELEASE = 1.2   // с, отпускание тормоза на выходе из поворота
const THROTTLE_OPEN = 0.5   // с, момент начала подачи газа
const THROTTLE_FULL = 2.2   // с, момент полностью открытого газа

/**
 * Положение педалей: одна плавная кривая на цикл, без дрожания.
 *
 * Шума здесь нет намеренно. Раньше к обеим педалям примешивались синусы 5 и
 * 8 Гц, а газ вычислялся как `1 - тормоз`, поэтому наследовал дрожание и от
 * тормоза, и от оборотов. При 60 кадрах в секунду это выглядело рваным.
 * Правдоподобия шум не добавлял: датчик положения педали шумит на порядок
 * меньше, чем видно на такой отрисовке.
 *
 * Фазы привязаны к тем же границам, что RPM, поэтому педали, обороты и
 * продольная перегрузка согласованы между собой. Обе кривые собраны из
 * smoothstep, то есть на стыках фаз производная нулевая — переломов нет.
 */
function computePedals(t: number): { brake: number; accel: number } {
  const phase = t % RPM_CYCLE
  const riseEnd = RPM_CYCLE - RPM_HOLD - RPM_DROP

  // Конец цикла: сброс газа и торможение в зону
  if (phase >= riseEnd) {
    const k = smoothstep(riseEnd, riseEnd + RPM_DROP, phase)
    return { brake: BRAKE_PEAK * k, accel: 1 - k }
  }

  // Выход из поворота: тормоз плавно отпускается с пиковых значений, газ
  // открывается с небольшим перекрытием — как на трейл-брейкинге
  const release = smoothstep(0, BRAKE_RELEASE, phase)
  return {
    brake: BRAKE_PEAK * (1 - release),
    accel: smoothstep(THROTTLE_OPEN, THROTTLE_FULL, phase),
  }
}

// ─── Главная функция сигналов ──────────────────────────────────────────────────

export function generateSignals(t: number): SignalValues {
  const rpm = computeRPM(t)

  // Скорость: интегрируем из оборотов с небольшим лагом
  const speed = Math.max(0, rpm * 0.022 - 26 + noise(t, 0.3, 3))

  // Температура ОЖ: прогрев от 20 до 92 °C за первые 45 с
  const coolant = Math.min(93, 20 + 73 * smoothstep(0, 45, t)) + noise(t, 0.04, 0.8)

  // Давление масла: 3–6 бар в норме, каждые 15 с — провал 200 мс до 0.5 бар
  const oilNormal = 3.0 + (rpm / RPM_REDLINE) * 3.0 + noise(t, 2.1, 0.15)
  const oilCycle  = t % 15                    // 15-секундный цикл
  const dipActive = oilCycle > 14.8           // последние 200 мс цикла
  const oilP = dipActive ? 0.5 : Math.max(2.8, oilNormal)

  // EGT: греется на высоких оборотах
  const egt = 300 + 650 * smoothstep(2000, 6500, rpm) + noise(t, 3.1, 40)

  // Нагрузка двигателя (%)
  const load = Math.min(100, 40 + 60 * smoothstep(2500, 7000, rpm))

  // Борт. напряжение
  const battery = 13.8 + noise(t, 0.31, 0.4)

  // ── G-сила (реалистичная трек-симуляция) ────────────────────────────────────
  // Продольная: положительная при разгоне, отрицательная при торможении
  const { brake, accel } = computePedals(t)
  const longG = accel * 0.7 - brake * 1.2

  // ── Руль — единый источник для steer.pos и поперечной перегрузки ──────────
  // Физически поворот руля ПОРОЖДАЕТ боковое ускорение, поэтому оба сигнала
  // считаются из одной величины: steer.pos > 0.5  ⟺  imu.ax > 0.
  const steerRaw  = Math.sin(t * 0.44) + 0.22 * Math.sin(t * 1.31)
  const steerNorm = Math.max(-1, Math.min(1, steerRaw / 1.22))   // −1 (влево) … +1 (вправо)

  // Поперечная G: пропорциональна углу руля, но только на скорости
  const latG = 0.95 * steerNorm * smoothstep(40, 95, speed)

  const brakePos = Math.max(0, Math.min(1, brake))
  const accelPos = Math.max(0, Math.min(1, accel))

  // 0 = полностью влево, 0.5 = центр, 1 = полностью вправо
  const steerClamped = Math.max(0, Math.min(1, 0.5 + 0.42 * steerNorm))

  // ── Engine timing / MAP / IAT ─────────────────────────────────────────────
  const engineMap = 30 + load * 0.7 + noise(t, 1.2, 3)
  const engineTiming = 12 + 14 * smoothstep(1500, 6000, rpm) - 4 * smoothstep(6000, 7400, rpm)
  const engineIat = 25 + noise(t, 0.07, 2)
  const oilT = Math.min(110, 40 + 70 * smoothstep(0, 120, t)) + noise(t, 0.05, 1)

  return {
    // Engine
    'engine.rpm':      rpm,
    'engine.coolant_t': coolant,
    'engine.load':     load,
    'engine.map':      engineMap,
    'engine.timing':   engineTiming,
    'engine.iat':      engineIat,
    // Vehicle
    'veh.speed':       speed,
    // Sensors
    'sensor.oil_p':    oilP,
    'sensor.oil_t':    oilT,
    'sensor.egt':      egt,
    // System
    'sys.battery':     battery,
    // IMU
    'imu.ax':          latG,
    'imu.ay':          longG,
    // Pedals
    'pedal.brake':     brakePos,
    'pedal.accel':     accelPos,
    // Steering (0 = full left, 0.5 = centre, 1 = full right)
    'steer.pos':       steerClamped,
    // Calculated
    'calc.oil_p_margin': oilP - Math.max(0.8, rpm / 2000),
  }
}

// ─── Подписка на RAF-тик ──────────────────────────────────────────────────────

type Listener = (values: SignalValues) => void

let rafId: number | null = null
const listeners = new Set<Listener>()
let startTime = 0

function tick() {
  if (listeners.size === 0) { rafId = null; return }
  const t = (performance.now() - startTime) / 1000
  const values = generateSignals(t)
  listeners.forEach(fn => fn(values))
  rafId = requestAnimationFrame(tick)
}

export function subscribeSignals(fn: Listener): () => void {
  if (listeners.size === 0) {
    startTime = performance.now()
    rafId = requestAnimationFrame(tick)
  }
  listeners.add(fn)
  return () => {
    listeners.delete(fn)
    if (listeners.size === 0 && rafId !== null) {
      cancelAnimationFrame(rafId)
      rafId = null
    }
  }
}
