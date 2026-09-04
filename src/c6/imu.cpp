#include "imu.h"
#include "i2cbus.h"
#include <Wire.h>
#include <math.h>

namespace imu {

// Registers.
static const uint8_t REG_WHOAMI = 0x00;   // reads 0x05
static const uint8_t REG_CTRL1  = 0x02;
static const uint8_t REG_CTRL2  = 0x03;   // accelerometer range / rate
static const uint8_t REG_CTRL7  = 0x08;   // which sensors are enabled
static const uint8_t REG_AX_L   = 0x35;   // ax, ay, az, 16-bit little-endian

static uint8_t addr = 0;
static bool    ok   = false;
static int     rot  = 0;

bool present() { return ok; }
int  rotation() { return rot; }

bool begin()
{
    i2cbus::begin();

    // Try both strap addresses and confirm with WHO_AM_I rather than trusting
    // an ACK: other parts live on this bus.
    for (uint8_t a : { ADDR_LOW, ADDR_HIGH }) {
        uint8_t who = 0;
        if (i2cbus::read8(a, REG_WHOAMI, who) && who == 0x05) { addr = a; break; }
    }
    if (!addr) { ok = false; return false; }

    // Address auto-increment on reads, which the burst below relies on.
    i2cbus::write8(addr, REG_CTRL1, 0x40);
    // Accelerometer: +/-4 g, 250 Hz. Neither matters much for a gravity
    // vector - the range only has to cover 1 g and the rate only has to beat
    // how fast a wrist turns.
    i2cbus::write8(addr, REG_CTRL2, 0x24);
    // Accelerometer on, gyroscope off.
    i2cbus::write8(addr, REG_CTRL7, 0x01);

    ok = true;
    poll();
    return true;
}

void poll()
{
    if (!ok) return;

    uint8_t b[6];
    {
        i2cbus::Hold h;
        Wire.beginTransmission(addr);
        Wire.write(REG_AX_L);
        if (Wire.endTransmission(false) != 0) return;
        if (Wire.requestFrom((int)addr, 6) != 6) return;
        for (int i = 0; i < 6; i++) b[i] = Wire.read();
    }

    int16_t ax = (int16_t)(b[0] | (b[1] << 8));
    int16_t ay = (int16_t)(b[2] | (b[3] << 8));

    // Gravity in the screen plane. Face down or face up leaves both near
    // zero, and there is no meaningful rotation then - keep the last one
    // rather than snapping to something arbitrary.
    const int32_t MIN_G = 3000;                  // counts, well under 1 g
    if ((int32_t)ax * ax + (int32_t)ay * ay < (int32_t)MIN_G * MIN_G) return;

    // Angle of the down-vector, and from it the quarter turn that puts the
    // face upright. atan2 is once per poll, not per pixel, so its cost is
    // irrelevant here.
    float deg = atan2f((float)ax, (float)ay) * 57.29578f;
    if (deg < 0) deg += 360.0f;

    // Hysteresis: a quadrant is entered at 45 degrees either side of its
    // centre but only left at 60, so a watch held near a diagonal settles
    // instead of flipping back and forth.
    int   want   = rot;
    float centre = rot * 90.0f;
    float diff   = deg - centre;
    while (diff < -180.0f) diff += 360.0f;
    while (diff >  180.0f) diff -= 360.0f;
    if (fabsf(diff) > 60.0f) {
        want = (int)((deg + 45.0f) / 90.0f) & 3;
    }
    rot = want;
}

} // namespace imu
