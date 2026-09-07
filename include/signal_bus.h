/**
 * @file  signal_bus.h
 * @brief Шина сигналов: id → текущее значение.
 *
 * Единственный источник данных для рендера. Кто именно её наполняет —
 * симуляция, CAN или внешние датчики — рендеру безразлично.
 *
 * Идентификаторы совпадают с редактором: "engine.rpm", "veh.speed",
 * "sensor.oil_p" и т.д.
 */

#pragma once

#include <Arduino.h>

namespace Signals {

/// Слотов под сигналы. Считать так: базовых около 20, плюс температура шин —
/// 4 колеса × (4 сектора + среднее) = 20. Запас нужен под датчики, которые
/// добавятся позже (давления, температуры, GPS), поэтому взято с двойным.
constexpr uint8_t kMaxSignals = 72;
constexpr uint8_t kMaxIdLen   = 24;

/// Записать значение. Новый id занимает свободный слот.
void set(const char* id, float value);

/// Прочитать значение. Если id нет — вернуть fallback.
float get(const char* id, float fallback = 0.0f);

bool has(const char* id);

void clear();

uint8_t count();

} // namespace Signals
