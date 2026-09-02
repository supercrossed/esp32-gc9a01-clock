#include "touch.h"
#include <Wire.h>

namespace touch {

static const int     PIN_SDA = 18, PIN_SCL = 8;
static const uint8_t ADDR    = 0x38;
static const uint32_t POLL_MS = 20;

// gesture thresholds, panel pixels / ms
static const int      SWIPE_MIN   = 70;     // travel needed to count as a swipe
static const int      MOVE_SLOP   = 25;     // less than this is "held still"
static const uint32_t LONG_MS     = 1200;
static const uint32_t TAP_MAX_MS  = 400;

static bool     down = false, moved = false, longFired = false;
static int      sx, sy, lx, ly;
static uint32_t t0, lastPoll;

bool begin()
{
    Wire.begin(PIN_SDA, PIN_SCL, 400000);
    Wire.beginTransmission(ADDR);
    return Wire.endTransmission() == 0;
}

// One read: touch count then X/Y of the first point, as the vendor does it.
static bool read(int &x, int &y)
{
    Wire.beginTransmission(ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADDR, 5) != 5) return false;
    uint8_t b[5];
    for (int i = 0; i < 5; i++) b[i] = Wire.read();
    if ((b[0] & 0x0F) == 0) return false;
    x = ((b[1] & 0x0F) << 8) | b[2];
    y = ((b[3] & 0x0F) << 8) | b[4];
    return true;
}

Gesture poll()
{
    uint32_t now = millis();
    if (now - lastPoll < POLL_MS) return NONE;
    lastPoll = now;

    int x, y;
    bool on = read(x, y);

    if (on && !down) {                       // finger down
        down = true; moved = false; longFired = false;
        sx = lx = x; sy = ly = y; t0 = now;
        return NONE;
    }
    if (on) {                                // still down
        lx = x; ly = y;
        if (abs(lx - sx) > MOVE_SLOP || abs(ly - sy) > MOVE_SLOP) moved = true;
        if (!moved && !longFired && now - t0 > LONG_MS) { longFired = true; return LONG_PRESS; }
        return NONE;
    }
    if (!down) return NONE;
    down = false;                            // finger up: what was that?
    int dx = lx - sx, dy = ly - sy;
    if (abs(dx) >= SWIPE_MIN && abs(dx) * 2 > abs(dy) * 3)
        return dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT;
    if (!moved && !longFired && now - t0 < TAP_MAX_MS) return TAP;
    return NONE;
}

bool pressed() { return down; }
int  x()       { return lx; }
int  y()       { return ly; }

} // namespace touch
