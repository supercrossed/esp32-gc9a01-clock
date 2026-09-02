#include "settings.h"
#include <Preferences.h>

Settings settings;
static const char *NS = "clock";

void settingsLoad()
{
    Preferences p;
    p.begin(NS, true);
    settings.ssid      = p.getString("ssid", "");
    settings.pass      = p.getString("pass", "");
    settings.locQuery  = p.getString("loc", "");
    settings.locName   = p.getString("locname", "");
    settings.tzName    = p.getString("tz", "");
    settings.lat       = p.getFloat("lat", 0);
    settings.lon       = p.getFloat("lon", 0);
    settings.hasLoc    = p.getBool("hasloc", false);
    settings.useF      = p.getBool("useF", true);
    settings.utcOffset = p.getInt("utcoff", 0);
    settings.hasOffset = p.getBool("hasoff", false);
    p.end();
}

void settingsSave()
{
    Preferences p;
    p.begin(NS, false);
    p.putString("ssid",    settings.ssid);
    p.putString("pass",    settings.pass);
    p.putString("loc",     settings.locQuery);
    p.putString("locname", settings.locName);
    p.putString("tz",      settings.tzName);
    p.putFloat ("lat",     settings.lat);
    p.putFloat ("lon",     settings.lon);
    p.putBool  ("hasloc",  settings.hasLoc);
    p.putBool  ("useF",    settings.useF);
    p.putInt   ("utcoff",  settings.utcOffset);
    p.putBool  ("hasoff",  settings.hasOffset);
    p.end();
}

void settingsClear()
{
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
    settings = Settings();
}
