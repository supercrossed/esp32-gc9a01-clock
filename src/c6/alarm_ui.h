// ---------------------------------------------------------------------------
//  The alarm interface: reached by holding a finger on the face, closed by
//  the Done button. Runs on the AMOLED board only - it is the only one with
//  a touchscreen and a speaker.
//
//  Three screens, in the shape of a phone's alarm app: a scrolling list with
//  per-alarm toggles and a +, an editor for one alarm, and the full-screen
//  ringer. main.cpp hands it every touch sample while it is open and calls
//  draw() at its own pace.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace alarm_ui {

void open();
void close();
bool running();

// The ringer takes over the screen; open() it from the alarm check.
void ring(int alarmIdx);
bool ringing();

// Feed one poll of the panel. Coordinates are panel pixels.
void touch(bool down, int x, int y);
// Repaint. Called from the render loop while running().
void draw();

} // namespace alarm_ui
