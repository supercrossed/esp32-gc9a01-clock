// Copy this file to config.h and fill it in. config.h is gitignored so your
// credentials stay out of the repo.
#pragma once

// ---- WiFi -------------------------------------------------------------
#define WIFI_SSID  "your-network"
#define WIFI_PASS  "your-password"

// ---- timezone ---------------------------------------------------------
// POSIX TZ string. A few common ones:
//   US Eastern   "EST5EDT,M3.2.0,M11.1.0"
//   US Central   "CST6CDT,M3.2.0,M11.1.0"
//   US Mountain  "MST7MDT,M3.2.0,M11.1.0"
//   US Pacific   "PST8PDT,M3.2.0,M11.1.0"
//   UK           "GMT0BST,M3.5.0/1,M10.5.0"
//   Central EU   "CET-1CEST,M3.5.0,M10.5.0/3"
//   India        "IST-5:30"
#define TZ_INFO    "EST5EDT,M3.2.0,M11.1.0"

// ---- location, for the weather ---------------------------------------
// Open-Meteo takes coordinates, not a postcode. Anywhere within a few miles
// of you reads the same. These are Orlando, FL.
#define WX_LAT     "28.4869"
#define WX_LON     "-81.4103"

// ---- units ------------------------------------------------------------
// "fahrenheit" or "celsius". The faces print the number as-is with an F
// suffix, so if you switch this you'll want to change that letter too.
#define WX_UNITS   "fahrenheit"
