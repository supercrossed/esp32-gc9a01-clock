// ---------------------------------------------------------------------------
//  PCF85063 real-time clock (U4 on the schematic, I2C 0x51).
//
//  Battery-backed on this board, so it keeps time while the ESP32 is off. It
//  is not a substitute for NTP - it drifts, and it does not know about time
//  zones or daylight saving - but it fills the gap NTP cannot:
//
//    * at boot there is a valid wall clock before WiFi has associated, so no
//      face has to show a placeholder time and no alarm is missed while the
//      network comes up.
//    * if the network stays down, time survives a reboot instead of resetting
//      to 1970.
//
//  The division of labour: the RTC seeds system time at boot, NTP corrects it
//  once the network is up, and the corrected time is written back to the RTC.
//
//  Times crossing this interface are UTC. The zone is applied by the same
//  configTzTime() the rest of the clock uses, so the RTC never has to know
//  which side of a DST change it is on.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>
#include <time.h>

namespace rtc {

static const uint8_t ADDR = 0x51;

// True if the part answered and is not reporting an invalid oscillator.
bool begin();
bool present();

// Read UTC. Returns 0 if the RTC has no plausible time (fresh cell, or the
// oscillator-stop flag is set), so a caller can tell "unset" from "1970".
time_t readUTC();

// Write UTC, after NTP has settled.
bool writeUTC(time_t t);

// Whether the last read looked like a real time.
bool valid();

} // namespace rtc
