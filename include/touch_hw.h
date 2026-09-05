#pragma once

#include <stdint.h>

/**
 * Тач CST9217 (контроллер CST92xx) по I2C.
 *
 * Свой драйвер писать не пришлось: у LilyGo есть SensorLib с классом
 * TouchDrvCST92xx, и именно его они используют в своих примерах для этой
 * панели. В Arduino_DriveBus поддержки CST9217 нет — там только FT3x68,
 * CST816x и CST2xxSE, поэтому берём SensorLib.
 */
namespace Touch {

struct Point {
    bool    pressed = false;
    int16_t x       = 0;
    int16_t y       = 0;
};

/// Инициализация I2C и контроллера. false — тач не отвечает.
bool begin();

/// Доступен ли тач (успешный begin).
bool available();

/// Текущее касание. Опрашивается по I2C, вызывать один раз за кадр.
Point poll();

} // namespace Touch
