#include "alarms.h"
#include <Preferences.h>
#include <stdio.h>

Alarms alarms;
static const char *NS = "alarms";

// The minute each alarm last rang, as (yday * 1440 + minute-of-day), so an
// alarm cannot fire twice inside its minute. -1 means "not yet today".
static long lastFired[ALARM_MAX];

void alarmsLoad()
{
    Preferences p;
    p.begin(NS, true);
    alarms.count  = p.getInt("count", 0);
    alarms.volume = (uint8_t)p.getUChar("vol", 100);
    if (alarms.count < 0) alarms.count = 0;
    if (alarms.count > ALARM_MAX) alarms.count = ALARM_MAX;
    // One blob per alarm rather than a packed struct: the layout can then
    // gain a field without invalidating what is already stored.
    for (int i = 0; i < alarms.count; i++) {
        char k[8];
        snprintf(k, sizeof k, "h%d", i); alarms.list[i].hour   = p.getUChar(k, 7);
        snprintf(k, sizeof k, "m%d", i); alarms.list[i].minute = p.getUChar(k, 0);
        snprintf(k, sizeof k, "o%d", i); alarms.list[i].on     = p.getBool (k, true);
        snprintf(k, sizeof k, "s%d", i); alarms.list[i].sound  = p.getUChar(k, 0);
        snprintf(k, sizeof k, "d%d", i); alarms.list[i].days   = p.getUChar(k, 0x7F);
    }
    p.end();
    for (int i = 0; i < ALARM_MAX; i++) lastFired[i] = -1;
}

void alarmsSave()
{
    Preferences p;
    p.begin(NS, false);
    p.putInt  ("count", alarms.count);
    p.putUChar("vol",   alarms.volume);
    for (int i = 0; i < alarms.count; i++) {
        char k[8];
        snprintf(k, sizeof k, "h%d", i); p.putUChar(k, alarms.list[i].hour);
        snprintf(k, sizeof k, "m%d", i); p.putUChar(k, alarms.list[i].minute);
        snprintf(k, sizeof k, "o%d", i); p.putBool (k, alarms.list[i].on);
        snprintf(k, sizeof k, "s%d", i); p.putUChar(k, alarms.list[i].sound);
        snprintf(k, sizeof k, "d%d", i); p.putUChar(k, alarms.list[i].days);
    }
    p.end();
}

int alarmsDue(const struct tm &t)
{
    long now = (long)t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
    for (int i = 0; i < alarms.count; i++) {
        const Alarm &a = alarms.list[i];
        if (!a.on) continue;
        if (!(a.days & (1u << t.tm_wday))) continue;
        if (a.hour != t.tm_hour || a.minute != t.tm_min) continue;
        if (lastFired[i] == now) continue;
        return i;
    }
    return -1;
}

void alarmsMarkFired(int idx, const struct tm &t)
{
    if (idx < 0 || idx >= ALARM_MAX) return;
    lastFired[idx] = (long)t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
}

void alarmFormat(const Alarm &a, char *out, size_t n)
{
    snprintf(out, n, "%02d:%02d", a.hour, a.minute);
}
