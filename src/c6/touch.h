// ---------------------------------------------------------------------------
//  FT6146 capacitive touch on the AMOLED board, over I2C, polled. Turns raw
//  presses into the few gestures the clock cares about.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace touch {

enum Gesture { NONE, SWIPE_LEFT, SWIPE_RIGHT, LONG_PRESS, TAP };

bool    begin();
Gesture poll();                    // call from the loop; a gesture is reported once
bool    pressed();                 // finger currently down
int     x();                       // last known position, panel pixels
int     y();

} // namespace touch
