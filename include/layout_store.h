/**
 * @file  layout_store.h
 * @brief Загрузка JSON-лейаута из LittleFS и доступ к его содержимому.
 *
 * Документ остаётся в памяти на всё время работы, а рендер читает пропы
 * виджетов прямо из него. Так мы не дублируем в C++ структуры всех типов
 * виджетов: добавление нового пропа в редакторе не требует правок парсера.
 * Лейаут — единицы килобайт, держать его целиком дешевле, чем поддерживать
 * зеркало схемы.
 */

#pragma once

#include <ArduinoJson.h>

namespace Layout {

/// Смонтировать LittleFS и загрузить лейаут (пробует несколько путей).
/// @return false — файл не найден или не разобран.
bool load();

/// Флаг "sim": { "enabled": true } из корня лейаута.
bool simEnabled();

uint8_t     screenCount();
const char* screenName(uint8_t idx);

/// Виджеты экрана. Пустой массив, если индекс вне диапазона.
JsonArrayConst widgets(uint8_t idx);

/// Фон экрана: screen.bg, иначе theme.bg.
uint16_t screenBg(uint8_t idx);

// Цвета темы (уже в RGB565)
uint16_t themeFg();
uint16_t themeMuted();
uint16_t themeWarn();
uint16_t themeCrit();

/// Путь, с которого лейаут был фактически загружен (для логов).
const char* loadedFrom();

} // namespace Layout
