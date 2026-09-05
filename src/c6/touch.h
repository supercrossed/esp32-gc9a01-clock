// ---------------------------------------------------------------------------
//  FT6146 capacitive touch on the AMOLED board, over I2C. Turns raw presses
//  into the few gestures the clock cares about.
//
//  Sampling runs on its own task, because a smooth face blocks the render
//  loop for a whole frame at a time and a swipe sampled that sparsely gets
//  missed. begin() starts it; poll() just collects what it found.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace touch {

enum Gesture { NONE, SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN,
               LONG_PRESS, TAP };

bool    begin();                   // starts the sampling task
Gesture poll();                    // next completed gesture, or NONE; reported once
bool    pressed();                 // finger currently down
int     x();                       // last known position, panel pixels
int     y();

} // namespace touch
