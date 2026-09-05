#include "touch_hw.h"

#include "pins.h"

// В SensorLib 0.2.6 драйверы тача лежат в подкаталоге touch/
#include <touch/TouchDrvCST92xx.h>
#include <Wire.h>

namespace Touch {

namespace {

/// Адрес зависит от прошивки тача. В примере LilyGo для CST9217 это 0x5A,
/// но встречается и 0x1A — если тач не найдётся, второй кандидат пробуется
/// автоматически.
constexpr uint8_t kAddrPrimary = 0x5A;
constexpr uint8_t kAddrAlt     = 0x1A;

TouchDrvCST92xx s_drv;
bool            s_ok = false;

} // namespace

bool begin() {
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    // Отдельного RST у тача на плате нет, только общая линия прерывания.
    // jumpCheck() из примера LilyGo здесь не вызывается: в SensorLib 0.2.6
    // этот метод убран из публичного API.
    s_drv.setPins(-1, TP_INT);

    uint8_t addr = kAddrPrimary;
    s_ok = s_drv.begin(Wire, addr, IIC_SDA, IIC_SCL);
    if (!s_ok) {
        addr = kAddrAlt;
        s_drv.setPins(-1, TP_INT);
        s_ok = s_drv.begin(Wire, addr, IIC_SDA, IIC_SCL);
    }

    if (!s_ok) {
        Serial.println("[touch] CST9217 не отвечает ни на 0x5A, ни на 0x1A");
        return false;
    }

    // Контроллер отдаёт координаты в своём разрешении; приводим к пикселям
    // панели, чтобы жесты считались в тех же единицах, что и лейаут.
    s_drv.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);

    Serial.printf("[touch] %s на 0x%02X\n", s_drv.getModelName(), addr);
    return true;
}

bool available() { return s_ok; }

Point poll() {
    Point pt;
    if (!s_ok) return pt;

    // Опрос, а не прерывание: линия INT общая с PCF8563, и вешать на неё
    // обработчик значит ловить чужие события. Одно чтение I2C на кадр дешевле
    // разбирательств, кто дёрнул линию.
    int16_t xs[5], ys[5];
    if (s_drv.getPoint(xs, ys, 1) > 0) {
        pt.pressed = true;
        pt.x       = xs[0];
        pt.y       = ys[0];
    }
    return pt;
}

} // namespace Touch
