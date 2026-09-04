#include "expander.h"
#include "i2cbus.h"

namespace expander {

// TCA9554 registers.
static const uint8_t REG_OUTPUT = 0x01;
static const uint8_t REG_CONFIG = 0x03;   // 1 = input, 0 = output

static uint8_t outShadow = 0x00;
static uint8_t cfgShadow = 0xFF;          // everything an input until claimed
static bool    ok = false;

bool begin()
{
    if (!i2cbus::present(ADDR)) { ok = false; return false; }
    // Read what is there rather than assuming a reset state: the panel's
    // lines are on this part too and are already driven by the time we run.
    uint8_t v;
    if (i2cbus::read8(ADDR, REG_OUTPUT, v)) outShadow = v;
    if (i2cbus::read8(ADDR, REG_CONFIG, v)) cfgShadow = v;
    ok = true;
    return true;
}

bool set(int pin, bool high)
{
    if (!ok || pin < 0 || pin > 7) return false;
    uint8_t bit = (uint8_t)(1u << pin);

    uint8_t nout = high ? (uint8_t)(outShadow | bit) : (uint8_t)(outShadow & ~bit);
    if (nout != outShadow) {
        if (!i2cbus::write8(ADDR, REG_OUTPUT, nout)) return false;
        outShadow = nout;
    }
    // Claim it as an output only after the level is set, so enabling the
    // amplifier cannot glitch it high for a transfer's worth of time.
    uint8_t ncfg = (uint8_t)(cfgShadow & ~bit);
    if (ncfg != cfgShadow) {
        if (!i2cbus::write8(ADDR, REG_CONFIG, ncfg)) return false;
        cfgShadow = ncfg;
    }
    return true;
}

} // namespace expander
