// ---------------------------------------------------------------------------
//  TCA9554 8-bit I2C expander (U1 on the schematic, address 0x20).
//
//  The pins the clock cares about are EXIO7, which enables the NS4150B
//  amplifier (PA_CTRL), and EXIO2/EXIO3, which the panel uses. Only the
//  amplifier is driven here; the display comes up without touching the rest.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace expander {

static const uint8_t ADDR = 0x20;
static const int     PA_CTRL = 7;      // EXIO7 -> NS4150B CTRL

bool begin();
// Drive one EXIO line. Returns false if the part did not acknowledge.
bool set(int pin, bool high);

} // namespace expander
