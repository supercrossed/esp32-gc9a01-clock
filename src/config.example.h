// ---------------------------------------------------------------------------
//  Optional. Nothing here is needed: on first boot the clock opens a hotspot
//  called "Clock-Setup" and everything below can be typed into the page it
//  serves. Values here are only defaults for a chip with nothing saved yet;
//  whatever is entered through the setup page takes over.
//
//  To use: copy to config.h and uncomment what you want. config.h is
//  gitignored so a password never ends up in the repo.
// ---------------------------------------------------------------------------
#pragma once

// #define WIFI_SSID    "your network"
// #define WIFI_PASS    "your password"

// Where you are, for the weather. A ZIP / postcode or a town name; it is
// looked up once on first connect and the timezone comes with it.
// #define WX_LOCATION  "32839"

// #define WX_UNITS     "fahrenheit"      // or "celsius"

// Only if you want to pin these rather than derive them from the location:
// #define WX_LAT       "28.4869"
// #define WX_LON       "-81.4103"
// #define TZ_INFO      "EST5EDT,M3.2.0,M11.1.0"   // POSIX rule string
