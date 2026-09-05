// ---------------------------------------------------------------------------
//  Alarms: a small list, kept in NVS beside the other settings so they
//  survive a power cut. Times are local wall-clock, 24 hour, matching every
//  digital face on this clock.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

static const int ALARM_MAX = 8;

struct Alarm {
    uint8_t hour = 7, minute = 0;
    bool    on   = true;
    uint8_t sound = 0;            // index into audio::Sound
    uint8_t days = 0x7F;          // bit 0 = Sunday .. bit 6 = Saturday; 0x7F = daily
};

struct Alarms {
    Alarm   list[ALARM_MAX];
    int     count = 0;
    uint8_t volume = 100;         // 0..100, shared by every alarm
    // Manual brightness trim, signed, added to the automatic day/night level.
    // Stored here because this is already the clock's small settings blob and
    // it saves a second NVS namespace for one number.
    int16_t brightAdjust = 0;
};

extern Alarms alarms;

void alarmsLoad();
void alarmsSave();

// Which alarm should be ringing at this local time, or -1. Fires once per
// minute per alarm: `lastFired` keeps a minute from re-triggering after a
// snooze-less dismissal.
int  alarmsDue(const struct tm &t);
// Called when the user dismisses, so the same minute does not ring again.
void alarmsMarkFired(int idx, const struct tm &t);

// Formatted "07:30" into a caller's buffer, for the UI.
void alarmFormat(const Alarm &a, char *out, size_t n);
