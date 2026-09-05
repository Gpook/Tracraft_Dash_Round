/**
 * @file  display_hw.h
 * @brief Инициализация AMOLED-панели и полнокадрового фреймбуфера в PSRAM.
 *
 * Рисуем всегда в канву (gfx()), затем один раз за кадр вызываем flush().
 */

#pragma once

#include <Arduino_GFX_Library.h>

namespace Display {

/**
 * Поднимает QSPI-шину, панель и канву.
 * @return false — не удалось выделить фреймбуфер или инициализировать панель.
 */
bool begin();

/// Цель отрисовки: фреймбуфер 466x466 RGB565 в PSRAM.
Arduino_GFX* gfx();

/// Отправить кадр на панель.
void flush();

/// Заливка всего фреймбуфера одним цветом.
/// Быстрее gfx()->fillScreen(): пишет 32-битными словами напрямую в буфер.
void clear(uint16_t color);

/// 0..255. Яркость всегда максимум во время езды — см. burn_in_protection.h.
void setBrightness(uint8_t v);

/// Размер фреймбуфера в байтах (для диагностики).
size_t framebufferBytes();

/// Сырой фреймбуфер 466x466 RGB565.
/// Нужен для эффектов, которых нет в Arduino_GFX: полупрозрачное затемнение
/// уже отрисованного кадра под всплывающим Warning и под shift-light flash.
uint16_t* framebuffer();

} // namespace Display
