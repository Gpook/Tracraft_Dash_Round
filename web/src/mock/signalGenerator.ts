/**
 * Генератор синтетических сигналов для предпросмотра редактора.
 *
 * Модель — круг по трассе, а не абстрактные пилы. Задана таблица поворотов, из
 * неё выводится всё остальное: профиль скорости, педали, перегрузки, передача,
 * обороты, температура шин. Поэтому сигналы согласованы между собой — руль,
 * боковая перегрузка и нагрев наружных шин приходят одновременно, как в жизни.
 *
 * Функция чистая: значения зависят только от t. Это важно в двух местах.
 * Во-первых, редактор можно скрести ползунком в обе стороны. Во-вторых, ровно
 * та же математика продублирована в src/sim_signals.cpp — при интегрировании
 * состояния предпросмотр и устройство разошлись бы после первого рассинхрона.
 *
 * ЛЮБАЯ правка формул здесь обязана повторяться в sim_signals.cpp.
 */

export type SignalValues = Record<string, number>

// ─── Вспомогательные ──────────────────────────────────────────────────────────

function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v
}

function smoothstep(a: number, b: number, x: number): number {
  const t = clamp((x - a) / (b - a), 0, 1)
  return t * t * (3 - 2 * t)
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * clamp(t, 0, 1)
}

function noise(t: number, freq: number, amp: number): number {
  return amp * Math.sin(t * freq * 2 * Math.PI)
}

/// Колокол единичной высоты. Основа профиля поворота: гладкий, с нулевой
/// производной на краях, поэтому склейка поворотов не даёт переломов.
function bell(x: number): number {
  return Math.exp(-x * x)
}

// ─── Трасса ───────────────────────────────────────────────────────────────────

/**
 * Круг: 78 секунд, девять поворотов и главная прямая.
 *
 * Прямая получается сама — это участок, где ни один колокол не дотягивается.
 * Между T9 (65 с) и T1 (7 с следующего круга) остаётся около 17 секунд, и на
 * них машина успевает разогнаться до отсечки в пятой передаче.
 *
 * dir   — знак поворота: +1 вправо, −1 влево (совпадает со знаком steer.pos)
 * grip  — требование к сцеплению 0..1, оно же глубина торможения
 * width — половина ширины колокола в секундах: чем меньше, тем резче поворот
 */
const LAP_TIME = 100.0   // круг 100 с — расслабленный темп, ~64 с на 78-секундном

// Тайминги и ширины масштабированы ×(100/78) от оригинального трека.
const CORNERS: { t: number; dir: number; grip: number; width: number }[] = [
  { t:  9.0, dir: +1.00, grip: 0.92, width: 2.8 },  // T1  правая шпилька после прямой
  { t: 17.5, dir: -0.55, grip: 0.45, width: 3.6 },  // T2  быстрый левый
  { t: 24.5, dir: -0.95, grip: 0.85, width: 2.6 },  // T3  левая шпилька
  { t: 34.5, dir: +0.45, grip: 0.35, width: 5.8 },  // T4  длинный правый вираж
  { t: 46.0, dir: +0.90, grip: 0.72, width: 1.9 },  // T5  шикана, правая
  { t: 50.0, dir: -0.90, grip: 0.72, width: 1.9 },  // T6  шикана, левая
  { t: 60.5, dir: -0.60, grip: 0.55, width: 4.5 },  // T7  затяжной левый
  { t: 73.0, dir: +1.00, grip: 0.95, width: 2.6 },  // T8  самая тугая правая
  { t: 83.5, dir: +0.35, grip: 0.28, width: 3.2 },  // T9  изгиб перед прямой
]

const V_MAX = 180.0   // км/ч на главной прямой (умеренный темп)
const V_MIN = 42.0    // км/ч в самой тугой шпильке

/// Расстояние до поворота с учётом того, что круг замкнут: T1 виден и в конце
/// круга, иначе на главной прямой был бы разрыв профиля.
function lapDelta(t: number, tc: number): number {
  let d = (t % LAP_TIME) - tc
  if (d > LAP_TIME / 2) d -= LAP_TIME
  if (d < -LAP_TIME / 2) d += LAP_TIME
  return d
}

/**
 * Загрузка поворотами в момент t.
 *
 * lag и spread сдвигают и расширяют колокола. При нулевых значениях это
 * мгновенная нагрузка (нужна для скорости и руля), при положительных —
 * тепловой отклик шины: резина продолжает греться и после вершины поворота.
 * Свёртка колокола с экспонентой отклика — это снова колокол, сдвинутый и
 * расширенный, поэтому запаздывание считается тем же кодом за один проход.
 */
function cornerLoad(t: number, lag = 0, spread = 1): number {
  let sum = 0
  for (const c of CORNERS) {
    sum += c.grip * bell(lapDelta(t, c.t + lag) / (c.width * spread))
  }
  return clamp(sum, 0, 1)
}

/// То же, но со знаком поворота: −1 (полностью влево) … +1 (полностью вправо).
function cornerSteer(t: number, lag = 0, spread = 1): number {
  let sum = 0
  for (const c of CORNERS) {
    sum += c.dir * c.grip * bell(lapDelta(t, c.t + lag) / (c.width * spread))
  }
  return clamp(sum, -1, 1)
}

/// Скорость: максимум на прямой, минимум в самом тугом повороте.
function speedAt(t: number): number {
  return V_MAX - (V_MAX - V_MIN) * cornerLoad(t)
}

// ─── Трансмиссия ──────────────────────────────────────────────────────────────

const RPM_IDLE    = 1150
const RPM_REDLINE = 7400

/// Скорость на отсечке в каждой передаче, км/ч.
/// 5-я передача: GEAR_TOP[4]=210 >> V_MAX=180 → на прямой RPM≈6340, ниже порога
/// allLit (7200). Отсечка срабатывает только при переключениях (1–2 с на передачу).
const GEAR_TOP = [58, 88, 122, 158, 210]

/// Передача по скорости. Порог *1.02 создаёт зону ~1.5–2 с на отсечке перед
/// каждым переключением — достаточно, чтобы shift-flash успел сработать.
function gearAt(speed: number): number {
  for (let i = 0; i < GEAR_TOP.length; ++i) {
    if (speed < GEAR_TOP[i] * 1.020) return i
  }
  return GEAR_TOP.length - 1
}

/**
 * Обороты из скорости и передачи.
 *
 * Жёсткий кэп на RPM_REDLINE: при позднем переключении (1.02 зона) rpm не
 * уходит выше отсечки, но allLit всё равно срабатывает, т.к. последний
 * stage.at ≤ RPM_REDLINE.
 */
function rpmAt(speed: number): number {
  const g = gearAt(speed)
  return Math.min(RPM_REDLINE, Math.max(RPM_IDLE, RPM_REDLINE * (speed / GEAR_TOP[g])))
}

// ─── Педали ───────────────────────────────────────────────────────────────────

/// Плечо численной производной скорости. 0.25 с достаточно мелко, чтобы
/// поймать вход в шпильку, и достаточно крупно, чтобы не ловить ступеньки.
const DV_DT = 0.25

const G_ACCEL_FULL = 0.55   // g при полностью открытом газе
const G_BRAKE_FULL = 1.15   // g при полном торможении

/**
 * Положение педалей из профиля скорости.
 *
 * Педали не задаются отдельной кривой, а выводятся из того, как меняется
 * скорость: замедление — это тормоз, ускорение — газ. Поэтому они не могут
 * разойтись с продольной перегрузкой и с оборотами.
 *
 * Шума здесь нет намеренно: датчик положения педали шумит на порядок меньше,
 * чем видно на такой отрисовке, а дрожание при 60 кадрах выглядело рваным.
 */
function pedalsAt(t: number): { brake: number; accel: number; longG: number } {
  // км/ч за секунду → g
  const dv = (speedAt(t + DV_DT) - speedAt(t - DV_DT)) / (2 * DV_DT)
  const longG = dv / 3.6 / 9.81

  const brake = clamp(-longG / G_BRAKE_FULL, 0, 1)

  // На прямой скорость уже максимальна и ускорения нет, но газ в полу —
  // иначе на самом длинном участке круга педаль показывала бы ноль.
  const fromG   = clamp(longG / G_ACCEL_FULL, 0, 1)
  const atSpeed = smoothstep(0.72, 0.94, speedAt(t) / V_MAX)
  const accel   = Math.max(fromG, atSpeed) * (1 - brake)

  return { brake, accel, longG }
}

// ─── Температура шин ──────────────────────────────────────────────────────────

const T_AMBIENT   = 24.0   // °C, холодная резина в боксе
const T_WORKING   = 76.0   // °C, средняя рабочая при спокойном темпе
const WARMUP_SEC  = 120.0  // за сколько секунд шины выходят на режим (≈1.2 круга)

/// Тепловой отклик резины: пик нагрева приходит после вершины поворота и
/// размазан по времени. Подставляется в cornerLoad как lag и spread.
const THERMAL_LAG    = 2.6
const THERMAL_SPREAD = 2.4

/// Тяга на выходе греет задние колёса позже, чем боковое скольжение — передние.
const TRACTION_LAG = THERMAL_LAG * 2.2

const LAT_GAIN      = 13.0  // °C прибавки нагруженному борту
const FRONT_GAIN    = 12.0  // °C прибавки переду от торможения и руления
const REAR_GAIN     = 9.0   // °C прибавки заду от тяги

/**
 * Профиль по секторам. Индексы 0..3 идут от НАРУЖНОГО плеча к ВНУТРЕННЕМУ.
 *
 * camber — отрицательный развал: на прямой внутреннее плечо в пятне контакта
 * работает больше и греется сильнее.
 * shoulder — в повороте нагруженное колесо кренится на наружное плечо, и
 * профиль частично переворачивается. Именно это соотношение и смотрят, когда
 * подбирают развал: если внутреннее плечо горячее даже в поворотах, развала
 * слишком много.
 */
const CAMBER_SHAPE   = [-1.0, -0.34, +0.34, +1.0]
const SHOULDER_SHAPE = [+1.0, +0.38, -0.38, -1.0]
const CAMBER_GAIN    = 7.0
const SHOULDER_GAIN  = 11.0

/// Индивидуальный разброс по углам, °C. Идеально симметричной машины не бывает.
const CORNER_TRIM = { fl: +1.5, fr: -0.8, rl: +0.4, rr: -1.2 }

type TireTemps = Record<string, number>

/**
 * Температуры шин: 4 колеса × 4 сектора плюс среднее по каждому колесу.
 *
 * Логика: общий прогрев за круг-полтора, сверху нагрев от текущей работы
 * (боковая нагрузка на борт, торможение на перед, тяга на зад), внутри колеса
 * распределение по секторам от развала и от переклада на наружное плечо.
 */
function tireTemps(t: number): TireTemps {
  // Общий прогрев от холодной резины к рабочей
  const base = lerp(T_AMBIENT, T_WORKING, smoothstep(0, WARMUP_SEC, t))

  // Запаздывающие нагрузки: тепло идёт за работой, а не мгновенно
  const heat     = cornerLoad(t, THERMAL_LAG, THERMAL_SPREAD)
  const steer    = cornerSteer(t, THERMAL_LAG, THERMAL_SPREAD)
  const traction = cornerLoad(t, TRACTION_LAG, THERMAL_SPREAD)

  // Поворот вправо (steer > 0) переносит вес на ЛЕВЫЙ борт — он и греется
  const load = {
    fl: Math.max(0, +steer), fr: Math.max(0, -steer),
    rl: Math.max(0, +steer), rr: Math.max(0, -steer),
  }

  // Ось: перед греется рулением и торможением, зад — тягой на выходе
  const axle = { fl: FRONT_GAIN * heat, fr: FRONT_GAIN * heat,
                 rl: REAR_GAIN * traction, rr: REAR_GAIN * traction }

  const out: TireTemps = {}
  for (const c of ['fl', 'fr', 'rl', 'rr'] as const) {
    const centre = base
      + LAT_GAIN * load[c] * (c[0] === 'f' ? 1.0 : 0.82)
      + axle[c]
      + CORNER_TRIM[c]
      + noise(t, 0.09, 0.6)

    let sum = 0
    for (let s = 0; s < 4; ++s) {
      const delta = CAMBER_GAIN * CAMBER_SHAPE[s]
                  + SHOULDER_GAIN * SHOULDER_SHAPE[s] * load[c]
      const v = centre + delta
      out[`tire.${c}.t${s + 1}`] = v
      sum += v
    }
    out[`tire.${c}.avg`] = sum / 4
  }
  return out
}

// ─── Главная функция сигналов ─────────────────────────────────────────────────

export function generateSignals(t: number): SignalValues {
  const speed = speedAt(t)
  const rpm   = rpmAt(speed)
  const gear  = gearAt(speed)

  const { brake, accel, longG } = pedalsAt(t)

  // Руль и боковая перегрузка из одного источника: поворот руля ПОРОЖДАЕТ
  // боковое ускорение, поэтому steer.pos > 0.5 ⟺ imu.ax > 0
  const steerNorm = cornerSteer(t)
  const latG      = 1.15 * steerNorm * smoothstep(30, 90, speed)

  // 0 = полностью влево, 0.5 = центр, 1 = полностью вправо
  const steerPos = clamp(0.5 + 0.45 * steerNorm, 0, 1)

  // Температура ОЖ: прогрев от 20 до 92 °C за первые 45 с
  const coolant = Math.min(93, 20 + 73 * smoothstep(0, 45, t)) + noise(t, 0.04, 0.8)

  // Давление масла: 3–6 бар в норме, каждые 15 с — провал 200 мс до 0.5 бар
  const oilNormal = 3.0 + (rpm / RPM_REDLINE) * 3.0 + noise(t, 2.1, 0.15)
  const dipActive = t % 15 > 14.8
  const oilP = dipActive ? 0.5 : Math.max(2.8, oilNormal)

  const oilT = Math.min(110, 40 + 70 * smoothstep(0, 120, t)) + noise(t, 0.05, 1)

  // Нагрузка двигателя: на прямой газ в полу, в повороте почти закрыт
  const load = clamp(8 + 92 * accel, 0, 100)

  const egt = 300 + 650 * smoothstep(0.2, 0.95, accel) * smoothstep(2500, 6500, rpm)
            + noise(t, 3.1, 40)

  const engineMap    = 30 + load * 0.7 + noise(t, 1.2, 3)
  const engineTiming = 12 + 14 * smoothstep(1500, 6000, rpm) - 4 * smoothstep(6000, 7400, rpm)
  const engineIat    = 25 + noise(t, 0.07, 2)
  const battery      = 13.8 + noise(t, 0.31, 0.4)

  return {
    // Engine
    'engine.rpm':       rpm,
    'engine.coolant_t': coolant,
    'engine.load':      load,
    'engine.map':       engineMap,
    'engine.timing':    engineTiming,
    'engine.iat':       engineIat,
    // Vehicle
    'veh.speed':        speed,
    'veh.gear':         gear + 1,
    // Sensors
    'sensor.oil_p':     oilP,
    'sensor.oil_t':     oilT,
    'sensor.egt':       egt,
    // System
    'sys.battery':      battery,
    // IMU
    'imu.ax':           latG,
    'imu.ay':           longG,
    // Pedals
    'pedal.brake':      brake,
    'pedal.accel':      accel,
    // Steering (0 = full left, 0.5 = centre, 1 = full right)
    'steer.pos':        steerPos,
    // Tyres: 4 колеса × 4 сектора (t1 наружное плечо … t4 внутреннее) + среднее
    ...tireTemps(t),
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
