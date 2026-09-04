// ---------------------------------------------------------------------------
//  The board's one I2C bus, shared by everything that hangs off it.
//
//  Touch (0x38) is polled from its own task while the codec (0x18) and the
//  TCA9554 expander (0x20) are driven from the render loop, so every transfer
//  has to take the lock. Wire itself is not re-entrant.
//
//  Pins are IO18/IO8 per the board's GPIO table.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace i2cbus {

static const int PIN_SDA = 18, PIN_SCL = 8;

// Idempotent: the first caller sets the bus up, the rest just get the lock.
void begin();
void lock();
void unlock();

// Scoped lock, so an early return cannot leak it.
struct Hold {
    Hold()  { lock(); }
    ~Hold() { unlock(); }
};

// Single-register helpers, each taking the lock itself.
bool write8(uint8_t addr, uint8_t reg, uint8_t val);
bool read8 (uint8_t addr, uint8_t reg, uint8_t &val);
bool present(uint8_t addr);

} // namespace i2cbus
