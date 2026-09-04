// ---------------------------------------------------------------------------
//  QMI8658 6-axis IMU (U7 on the schematic), on the shared I2C bus.
//
//  Only the accelerometer is used, and only to answer one question: which way
//  is down? That gives the screen orientation, snapped to the nearest quarter
//  turn, so the face stays upright however the watch is held.
//
//  The gyro is left powered down. Tracking rotation rate would let the display
//  follow a turn more smoothly, but it costs current and drifts, and a clock
//  only needs to know which of four ways up it is.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace imu {

// The part answers at one of two addresses depending on its SA0 strap.
static const uint8_t ADDR_LOW  = 0x6A;
static const uint8_t ADDR_HIGH = 0x6B;

bool begin();
bool present();

// Quarter turns clockwise, 0..3, that the face should be drawn at for the
// current orientation. Hysteresis is applied inside, so a watch held near a
// diagonal does not flap between two rotations.
int  rotation();

// Sample the accelerometer and update rotation(). Cheap; call from the loop.
void poll();

} // namespace imu
