#include "sim_signals.h"
#include "signal_bus.h"

#include <math.h>

namespace Sim {

namespace {

// ─── Вспомогательные (соответствуют TS-версии) ───────────────────────────────

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float smoothstep(float a, float b, float x) {
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float lerpf(float a, float b, float t) {
    return a + (b - a) * clampf(t, 0.0f, 1.0f);
}

float noise(float t, float freq, float amp) {
    return amp * sinf(t * freq * 2.0f * PI);
}

// ─── RPM: разгон до отсечки, 200 мс зависание, сброс ─────────────────────────

constexpr float RPM_IDLE    = 1200.0f;
constexpr float RPM_REDLINE = 7400.0f;   // выше последнего порога 7200 → мигание
constexpr float RPM_CYCLE   = 7.0f;      // секунд на цикл
constexpr float RPM_HOLD    = 0.20f;     // удержание на отсечке
constexpr float RPM_DROP    = 0.35f;     // сброс газа

float computeRPM(float t) {
    const float phase   = fmodf(t, RPM_CYCLE);
    const float riseEnd = RPM_CYCLE - RPM_HOLD - RPM_DROP;

    if (phase < riseEnd) {
        const float f = phase / riseEnd;
        const float curved = (f < 0.6f)
            ? smoothstep(0.0f, 0.6f, f) * 0.65f
            : 0.65f + smoothstep(0.6f, 1.0f, f) * 0.35f;
        return lerpf(RPM_IDLE, RPM_REDLINE, curved);
    }
    if (phase < riseEnd + RPM_HOLD) {
        return RPM_REDLINE + noise(t, 22.0f, 60.0f);
    }
    const float dropF = (phase - riseEnd - RPM_HOLD) / RPM_DROP;
    return lerpf(RPM_REDLINE, RPM_IDLE + 400.0f, smoothstep(0.0f, 1.0f, dropF));
}

// ─── Педали ──────────────────────────────────────────────────────────────────

/// Пик торможения в конце цикла и он же — точка, с которой начинается отпуск
/// на выходе из поворота. Одно значение на оба конца делает кривую непрерывной
/// на стыке циклов.
constexpr float BRAKE_PEAK    = 0.9f;
constexpr float BRAKE_RELEASE = 1.2f;   // с, отпускание тормоза
constexpr float THROTTLE_OPEN = 0.5f;   // с, начало подачи газа
constexpr float THROTTLE_FULL = 2.2f;   // с, газ полностью открыт

struct Pedals { float brake, accel; };

/**
 * Положение педалей: одна плавная кривая на цикл, без дрожания.
 *
 * Шума здесь нет намеренно. Раньше к обеим педалям примешивались синусы 5 и
 * 8 Гц, а газ вычислялся как `1 - тормоз`, поэтому наследовал дрожание и от
 * тормоза, и от оборотов. Правдоподобия это не добавляло: датчик положения
 * педали шумит на порядок меньше, чем видно на такой отрисовке.
 *
 * Формулы совпадают с web/src/mock/signalGenerator.ts — менять только парой,
 * иначе предпросмотр и устройство разойдутся.
 */
Pedals computePedals(float t) {
    const float phase   = fmodf(t, RPM_CYCLE);
    const float riseEnd = RPM_CYCLE - RPM_HOLD - RPM_DROP;

    // Конец цикла: сброс газа и торможение в зону
    if (phase >= riseEnd) {
        const float k = smoothstep(riseEnd, riseEnd + RPM_DROP, phase);
        return Pedals{BRAKE_PEAK * k, 1.0f - k};
    }

    // Выход из поворота: тормоз плавно отпускается с пиковых значений, газ
    // открывается с небольшим перекрытием — как на трейл-брейкинге
    const float release = smoothstep(0.0f, BRAKE_RELEASE, phase);
    return Pedals{
        BRAKE_PEAK * (1.0f - release),
        smoothstep(THROTTLE_OPEN, THROTTLE_FULL, phase),
    };
}

} // namespace

void update(float t) {
    const float rpm = computeRPM(t);

    // Скорость: из оборотов с небольшим лагом
    const float speed = fmaxf(0.0f, rpm * 0.022f - 26.0f + noise(t, 0.3f, 3.0f));

    // Температура ОЖ: прогрев 20 → 92 °C за первые 45 с
    const float coolant =
        fminf(93.0f, 20.0f + 73.0f * smoothstep(0.0f, 45.0f, t)) + noise(t, 0.04f, 0.8f);

    // Давление масла: 3–6 бар, каждые 15 с провал 200 мс до 0.5 бар
    const float oilNormal = 3.0f + (rpm / RPM_REDLINE) * 3.0f + noise(t, 2.1f, 0.15f);
    const float oilCycle  = fmodf(t, 15.0f);
    const bool  dipActive = oilCycle > 14.8f;
    const float oilP      = dipActive ? 0.5f : fmaxf(2.8f, oilNormal);

    const float egt  = 300.0f + 650.0f * smoothstep(2000.0f, 6500.0f, rpm) + noise(t, 3.1f, 40.0f);
    const float load = fminf(100.0f, 40.0f + 60.0f * smoothstep(2500.0f, 7000.0f, rpm));
    const float battery = 13.8f + noise(t, 0.31f, 0.4f);

    // Продольная G: разгон плюс, торможение минус
    const Pedals ped  = computePedals(t);
    const float  longG = ped.accel * 0.7f - ped.brake * 1.2f;

    // Руль — единый источник для steer.pos и поперечной перегрузки:
    // поворот руля ПОРОЖДАЕТ боковое ускорение, поэтому steer.pos > 0.5 ⟺ imu.ax > 0
    const float steerRaw  = sinf(t * 0.44f) + 0.22f * sinf(t * 1.31f);
    const float steerNorm = clampf(steerRaw / 1.22f, -1.0f, 1.0f);
    const float latG      = 0.95f * steerNorm * smoothstep(40.0f, 95.0f, speed);

    const float brakePos = clampf(ped.brake, 0.0f, 1.0f);
    const float accelPos = clampf(ped.accel, 0.0f, 1.0f);

    // 0 = полностью влево, 0.5 = центр, 1 = полностью вправо
    const float steerPos = clampf(0.5f + 0.42f * steerNorm, 0.0f, 1.0f);

    const float engineMap    = 30.0f + load * 0.7f + noise(t, 1.2f, 3.0f);
    const float engineTiming = 12.0f + 14.0f * smoothstep(1500.0f, 6000.0f, rpm)
                             - 4.0f * smoothstep(6000.0f, 7400.0f, rpm);
    const float engineIat    = 25.0f + noise(t, 0.07f, 2.0f);
    const float oilT = fminf(110.0f, 40.0f + 70.0f * smoothstep(0.0f, 120.0f, t)) + noise(t, 0.05f, 1.0f);

    Signals::set("engine.rpm",       rpm);
    Signals::set("engine.coolant_t", coolant);
    Signals::set("engine.load",      load);
    Signals::set("engine.map",       engineMap);
    Signals::set("engine.timing",    engineTiming);
    Signals::set("engine.iat",       engineIat);

    Signals::set("veh.speed",        speed);

    Signals::set("sensor.oil_p",     oilP);
    Signals::set("sensor.oil_t",     oilT);
    Signals::set("sensor.egt",       egt);

    Signals::set("sys.battery",      battery);

    Signals::set("imu.ax",           latG);
    Signals::set("imu.ay",           longG);

    Signals::set("pedal.brake",      brakePos);
    Signals::set("pedal.accel",      accelPos);
    Signals::set("steer.pos",        steerPos);

    Signals::set("calc.oil_p_margin", oilP - fmaxf(0.8f, rpm / 2000.0f));
}

} // namespace Sim
