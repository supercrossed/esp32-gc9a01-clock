#include "i2cbus.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace i2cbus {

static SemaphoreHandle_t mtx = nullptr;
static bool started = false;

void begin()
{
    if (!mtx) mtx = xSemaphoreCreateMutex();
    if (started) return;
    started = true;
    Wire.begin(PIN_SDA, PIN_SCL, 400000);
}

void lock()   { if (mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
void unlock() { if (mtx) xSemaphoreGive(mtx); }

bool write8(uint8_t addr, uint8_t reg, uint8_t val)
{
    Hold h;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool read8(uint8_t addr, uint8_t reg, uint8_t &val)
{
    Hold h;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

bool present(uint8_t addr)
{
    Hold h;
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

} // namespace i2cbus
