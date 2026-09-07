/**
 * @file  sim_signals.cpp
 * @brief Симуляция сигналов на устройстве.
 *
 * Порт web/src/mock/signalGenerator.ts. Формулы и константы обязаны совпадать
 * с редактором один в один — иначе предпросмотр перестанет показывать то, что
 * увидит водитель. ЛЮБАЯ правка здесь повторяется там же, и наоборот.
 *
 * Модель — круг по трассе. Задана таблица поворотов, из неё выводится всё
 * остальное: профиль скорости, педали, перегрузки, передача, обороты, нагрев
 * шин. Поэтому сигналы согласованы между собой.
 *
 * Функция чистая: значения зависят только от t, состояние между кадрами не
 * копится. Интегрирование развело бы устройство с редактором после первого
 * пропущенного кадра.
 */

#include "sim_signals.h"
#include "signal_bus.h"

#include <math.h>
#include <stdio.h>

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

/// Колокол единичной высоты — основа профиля поворота.
float bell(float x) {
    return expf(-x * x);
}

// ─── Трасса ──────────────────────────────────────────────────────────────────

/// Круг: 78 с, девять поворотов и главная прямая. Прямая получается сама — это
/// Круг 100 с — расслабленный темп (≈ 78-секундный × 1.28).
/// Главная прямая: T9 (83.5 с) → T1 (9 с следующего круга) ≈ 25 с.
constexpr float LAP_TIME = 100.0f;

struct Corner {
    float t;      ///< центр поворота, с от начала круга
    float dir;    ///< +1 вправо, −1 влево (знак совпадает со steer.pos)
    float grip;   ///< требование к сцеплению 0..1, оно же глубина торможения
    float width;  ///< половина ширины колокола, с: меньше — резче поворот
};

// Тайминги масштабированы ×(100/78) от оригинального трека.
constexpr Corner CORNERS[] = {
    {  9.0f, +1.00f, 0.92f, 2.8f },  // T1  правая шпилька после прямой
    { 17.5f, -0.55f, 0.45f, 3.6f },  // T2  быстрый левый
    { 24.5f, -0.95f, 0.85f, 2.6f },  // T3  левая шпилька
    { 34.5f, +0.45f, 0.35f, 5.8f },  // T4  длинный правый вираж
    { 46.0f, +0.90f, 0.72f, 1.9f },  // T5  шикана, правая
    { 50.0f, -0.90f, 0.72f, 1.9f },  // T6  шикана, левая
    { 60.5f, -0.60f, 0.55f, 4.5f },  // T7  затяжной левый
    { 73.0f, +1.00f, 0.95f, 2.6f },  // T8  самая тугая правая
    { 83.5f, +0.35f, 0.28f, 3.2f },  // T9  изгиб перед прямой
};
constexpr int N_CORNERS = sizeof(CORNERS) / sizeof(CORNERS[0]);

constexpr float V_MAX = 180.0f;   // км/ч на главной прямой (умеренный темп)
constexpr float V_MIN = 42.0f;    // км/ч в самой тугой шпильке

/// Расстояние до поворота с учётом замкнутости круга: T1 виден и в конце
/// круга, иначе на главной прямой был бы разрыв профиля.
float lapDelta(float t, float tc) {
    float d = fmodf(t, LAP_TIME) - tc;
    if (d >  LAP_TIME * 0.5f) d -= LAP_TIME;
    if (d < -LAP_TIME * 0.5f) d += LAP_TIME;
    return d;
}

/**
 * Загрузка поворотами в момент t.
 *
 * lag и spread сдвигают и расширяют колокола. При нулевом лаге это мгновенная
 * нагрузка (нужна для скорости и руля), при положительном — тепловой отклик
 * шины: резина продолжает греться и после вершины поворота. Свёртка колокола
 * с экспонентой отклика — снова колокол, сдвинутый и расширенный, поэтому
 * запаздывание считается тем же кодом за один проход.
 */
float cornerLoad(float t, float lag = 0.0f, float spread = 1.0f) {
    float sum = 0.0f;
    for (int i = 0; i < N_CORNERS; ++i) {
        sum += CORNERS[i].grip *
               bell(lapDelta(t, CORNERS[i].t + lag) / (CORNERS[i].width * spread));
    }
    return clampf(sum, 0.0f, 1.0f);
}

/// То же, но со знаком поворота: −1 (полностью влево) … +1 (полностью вправо).
float cornerSteer(float t, float lag = 0.0f, float spread = 1.0f) {
    float sum = 0.0f;
    for (int i = 0; i < N_CORNERS; ++i) {
        sum += CORNERS[i].dir * CORNERS[i].grip *
               bell(lapDelta(t, CORNERS[i].t + lag) / (CORNERS[i].width * spread));
    }
    return clampf(sum, -1.0f, 1.0f);
}

/// Скорость: максимум на прямой, минимум в самом тугом повороте.
float speedAt(float t) {
    return V_MAX - (V_MAX - V_MIN) * cornerLoad(t);
}

// ─── Трансмиссия ─────────────────────────────────────────────────────────────

constexpr float RPM_IDLE    = 1150.0f;
constexpr float RPM_REDLINE = 7400.0f;

/// Скорость на отсечке в каждой передаче, км/ч.
/// 5-я: GEAR_TOP[4]=210 >> V_MAX=180 → RPM≈6340 на прямой, без постоянной отсечки.
/// Отсечка (~7200 RPM) срабатывает только при переключениях (1–2 с на передачу).
constexpr float GEAR_TOP[] = { 58.0f, 88.0f, 122.0f, 158.0f, 210.0f };
constexpr int   N_GEARS    = sizeof(GEAR_TOP) / sizeof(GEAR_TOP[0]);

/// Передача по скорости. Порог *1.02 создаёт зону ~1.5 с на отсечке перед
/// каждым переключением — достаточно для shift-flash.
int gearAt(float speed) {
    for (int i = 0; i < N_GEARS; ++i) {
        if (speed < GEAR_TOP[i] * 1.020f) return i;
    }
    return N_GEARS - 1;
}

/// Обороты из скорости и передачи. Разрыв на переключении сделан намеренно:
/// настоящий тахометр так и падает, и это же оживляет шифт-лайт — он набирает
/// шкалу заново в каждой передаче вместо одной длинной пилы за круг.
/// Кэп на RPM_REDLINE: в зоне *1.02 обороты не уходят выше отсечки.
float rpmAt(float speed) {
    const float raw = fmaxf(RPM_IDLE, RPM_REDLINE * (speed / GEAR_TOP[gearAt(speed)]));
    return fminf(RPM_REDLINE, raw);
}

// ─── Педали ──────────────────────────────────────────────────────────────────

/// Плечо численной производной скорости: достаточно мелко, чтобы поймать вход
/// в шпильку, и достаточно крупно, чтобы не ловить ступеньки.
constexpr float DV_DT = 0.25f;

constexpr float G_ACCEL_FULL = 0.55f;   // g при полностью открытом газе
constexpr float G_BRAKE_FULL = 1.15f;   // g при полном торможении

struct Pedals { float brake, accel, longG; };

/**
 * Положение педалей из профиля скорости.
 *
 * Педали не задаются отдельной кривой, а выводятся из того, как меняется
 * скорость: замедление — тормоз, ускорение — газ. Поэтому они не могут
 * разойтись с продольной перегрузкой и с оборотами.
 *
 * Шума нет намеренно: датчик положения педали шумит на порядок меньше, чем
 * видно на такой отрисовке, а дрожание при высоком fps выглядело рваным.
 */
Pedals pedalsAt(float t) {
    // км/ч за секунду → g
    const float dv    = (speedAt(t + DV_DT) - speedAt(t - DV_DT)) / (2.0f * DV_DT);
    const float longG = dv / 3.6f / 9.81f;

    const float brake = clampf(-longG / G_BRAKE_FULL, 0.0f, 1.0f);

    // На прямой скорость уже максимальна и ускорения нет, но газ в полу —
    // иначе на самом длинном участке круга педаль показывала бы ноль.
    const float fromG   = clampf(longG / G_ACCEL_FULL, 0.0f, 1.0f);
    const float atSpeed = smoothstep(0.72f, 0.94f, speedAt(t) / V_MAX);
    const float accel   = fmaxf(fromG, atSpeed) * (1.0f - brake);

    return Pedals{brake, accel, longG};
}

// ─── Температура шин ─────────────────────────────────────────────────────────

constexpr float T_AMBIENT  = 24.0f;   // °C, холодная резина в боксе
constexpr float T_WORKING  = 76.0f;   // °C, средняя рабочая при спокойном темпе
constexpr float WARMUP_SEC = 120.0f;  // за сколько секунд шины выходят на режим (≈1.2 круга)

/// Тепловой отклик резины: пик нагрева приходит после вершины поворота и
/// размазан по времени. Подставляется в cornerLoad как lag и spread.
constexpr float THERMAL_LAG    = 2.6f;
constexpr float THERMAL_SPREAD = 2.4f;

/// Тяга на выходе греет задние колёса позже, чем скольжение — передние.
constexpr float TRACTION_LAG = THERMAL_LAG * 2.2f;

constexpr float LAT_GAIN   = 13.0f;  // °C прибавки нагруженному борту
constexpr float FRONT_GAIN = 12.0f;  // °C прибавки переду от торможения и руления
constexpr float REAR_GAIN  =  9.0f;  // °C прибавки заду от тяги

/**
 * Профиль по секторам. Индексы 0..3 идут от НАРУЖНОГО плеча к ВНУТРЕННЕМУ.
 *
 * CAMBER — отрицательный развал: на прямой внутреннее плечо в пятне контакта
 * работает больше и греется сильнее.
 * SHOULDER — в повороте нагруженное колесо кренится на наружное плечо, и
 * профиль частично переворачивается. Именно это соотношение и смотрят, когда
 * подбирают развал: если внутреннее плечо горячее даже в поворотах, развала
 * слишком много.
 */
constexpr float CAMBER_SHAPE[4]   = { -1.00f, -0.34f, +0.34f, +1.00f };
constexpr float SHOULDER_SHAPE[4] = { +1.00f, +0.38f, -0.38f, -1.00f };
constexpr float CAMBER_GAIN       = 7.0f;
constexpr float SHOULDER_GAIN     = 11.0f;

/// Углы в порядке fl, fr, rl, rr. front — передняя ось, trim — индивидуальный
/// разброс в °C: идеально симметричной машины не бывает.
struct CornerSpec {
    const char* key;
    bool        left;
    bool        front;
    float       trim;
};

constexpr CornerSpec CORNER_SPECS[4] = {
    { "fl", true,  true,  +1.5f },
    { "fr", false, true,  -0.8f },
    { "rl", true,  false, +0.4f },
    { "rr", false, false, -1.2f },
};

/**
 * Температуры шин: 4 колеса × 4 сектора плюс среднее по каждому колесу.
 *
 * Логика: общий прогрев за круг-полтора, сверху нагрев от текущей работы
 * (боковая нагрузка на борт, торможение на перед, тяга на зад), внутри колеса
 * распределение по секторам от развала и от переклада на наружное плечо.
 */
void setTireTemps(float t) {
    // Общий прогрев от холодной резины к рабочей
    const float base = lerpf(T_AMBIENT, T_WORKING, smoothstep(0.0f, WARMUP_SEC, t));

    // Запаздывающие нагрузки: тепло идёт за работой, а не мгновенно
    const float heat     = cornerLoad(t, THERMAL_LAG, THERMAL_SPREAD);
    const float steer    = cornerSteer(t, THERMAL_LAG, THERMAL_SPREAD);
    const float traction = cornerLoad(t, TRACTION_LAG, THERMAL_SPREAD);

    // Поворот вправо (steer > 0) переносит вес на ЛЕВЫЙ борт — он и греется
    const float leftLoad  = fmaxf(0.0f, +steer);
    const float rightLoad = fmaxf(0.0f, -steer);

    const float wobble = noise(t, 0.09f, 0.6f);

    char id[Signals::kMaxIdLen];

    for (int c = 0; c < 4; ++c) {
        const CornerSpec& cs = CORNER_SPECS[c];
        const float load = cs.left ? leftLoad : rightLoad;

        // Ось: перед греется рулением и торможением, зад — тягой на выходе
        const float axle = cs.front ? FRONT_GAIN * heat : REAR_GAIN * traction;

        const float centre = base
                           + LAT_GAIN * load * (cs.front ? 1.0f : 0.82f)
                           + axle
                           + cs.trim
                           + wobble;

        float sum = 0.0f;
        for (int s = 0; s < 4; ++s) {
            const float delta = CAMBER_GAIN * CAMBER_SHAPE[s]
                              + SHOULDER_GAIN * SHOULDER_SHAPE[s] * load;
            const float v = centre + delta;

            snprintf(id, sizeof id, "tire.%s.t%d", cs.key, s + 1);
            Signals::set(id, v);
            sum += v;
        }

        snprintf(id, sizeof id, "tire.%s.avg", cs.key);
        Signals::set(id, sum * 0.25f);
    }
}

} // namespace

void update(float t) {
    const float speed = speedAt(t);
    const float rpm   = rpmAt(speed);
    const int   gear  = gearAt(speed);

    const Pedals ped = pedalsAt(t);

    // Руль и боковая перегрузка из одного источника: поворот руля ПОРОЖДАЕТ
    // боковое ускорение, поэтому steer.pos > 0.5 ⟺ imu.ax > 0
    const float steerNorm = cornerSteer(t);
    const float latG      = 1.15f * steerNorm * smoothstep(30.0f, 90.0f, speed);

    // 0 = полностью влево, 0.5 = центр, 1 = полностью вправо
    const float steerPos = clampf(0.5f + 0.45f * steerNorm, 0.0f, 1.0f);

    // Температура ОЖ: прогрев 20 → 92 °C за первые 45 с
    const float coolant =
        fminf(93.0f, 20.0f + 73.0f * smoothstep(0.0f, 45.0f, t)) + noise(t, 0.04f, 0.8f);

    // Давление масла: 3–6 бар, каждые 15 с провал 200 мс до 0.5 бар
    const float oilNormal = 3.0f + (rpm / RPM_REDLINE) * 3.0f + noise(t, 2.1f, 0.15f);
    const bool  dipActive = fmodf(t, 15.0f) > 14.8f;
    const float oilP      = dipActive ? 0.5f : fmaxf(2.8f, oilNormal);

    const float oilT =
        fminf(110.0f, 40.0f + 70.0f * smoothstep(0.0f, 120.0f, t)) + noise(t, 0.05f, 1.0f);

    // Нагрузка двигателя: на прямой газ в полу, в повороте почти закрыт
    const float load = clampf(8.0f + 92.0f * ped.accel, 0.0f, 100.0f);

    const float egt = 300.0f
                    + 650.0f * smoothstep(0.2f, 0.95f, ped.accel)
                             * smoothstep(2500.0f, 6500.0f, rpm)
                    + noise(t, 3.1f, 40.0f);

    const float engineMap    = 30.0f + load * 0.7f + noise(t, 1.2f, 3.0f);
    const float engineTiming = 12.0f + 14.0f * smoothstep(1500.0f, 6000.0f, rpm)
                             - 4.0f * smoothstep(6000.0f, 7400.0f, rpm);
    const float engineIat    = 25.0f + noise(t, 0.07f, 2.0f);
    const float battery      = 13.8f + noise(t, 0.31f, 0.4f);

    Signals::set("engine.rpm",       rpm);
    Signals::set("engine.coolant_t", coolant);
    Signals::set("engine.load",      load);
    Signals::set("engine.map",       engineMap);
    Signals::set("engine.timing",    engineTiming);
    Signals::set("engine.iat",       engineIat);

    Signals::set("veh.speed",        speed);
    Signals::set("veh.gear",         static_cast<float>(gear + 1));

    Signals::set("sensor.oil_p",     oilP);
    Signals::set("sensor.oil_t",     oilT);
    Signals::set("sensor.egt",       egt);

    Signals::set("sys.battery",      battery);

    Signals::set("imu.ax",           latG);
    Signals::set("imu.ay",           ped.longG);

    Signals::set("pedal.brake",      ped.brake);
    Signals::set("pedal.accel",      ped.accel);
    Signals::set("steer.pos",        steerPos);

    setTireTemps(t);

    Signals::set("calc.oil_p_margin", oilP - fmaxf(0.8f, rpm / 2000.0f));
}

} // namespace Sim
