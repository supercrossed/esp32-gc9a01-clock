// ---------------------------------------------------------------------------
//  NTP clock + weather  -  ESP32-S3 / ESP32-C3 Super Mini + GC9A01 round LCD,
//                          and the Waveshare ESP32-C6 1.43" AMOLED
//
//  This file owns WiFi, NTP, the weather fetch, the face rotation and touch.
//  The display itself is behind screen.h, with one back end per board
//  family. The visual design lives in faces/. Settings (WiFi, location,
//  units) live in NVS and are entered through the captive setup hotspot in
//  portal.cpp, so nothing has to be compiled in.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include "face.h"
#include "screen.h"
#include "settings.h"
#include "portal.h"
#include "tz.h"
#ifdef AMOLED_C6
#include "c6/touch.h"
#endif

// ----------------------------- compiled-in defaults ------------------------
// config.h is optional. If present, its values seed a chip that has nothing
// saved yet; anything entered through the setup page wins over them.
#if __has_include("config.h")
#include "config.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef WX_LOCATION
#define WX_LOCATION ""
#endif
#ifndef WX_UNITS
#define WX_UNITS "fahrenheit"
#endif
#ifdef TZ_INFO
static const char *CFG_TZ = TZ_INFO;
#else
static const char *CFG_TZ = nullptr;
#endif

// Weather: Open-Meteo, which needs no API key.
static const uint32_t WX_PERIOD_MS = 10UL * 60UL * 1000UL;  // refresh every 10 min
static const uint32_t WX_RETRY_MS  = 30UL * 1000UL;         // sooner after a failure

// How long the network can be gone before the setup hotspot reopens.
static const uint32_t PORTAL_AFTER_DOWN_MS = 60UL * 1000UL;

// BOOT button: hold at power-on to wipe the settings and start setup over.
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
#define BOOT_BTN 9
#else
#define BOOT_BTN 0
#endif

// ---------------------------------------------------------------------------

// ---- face rotation --------------------------------------------------------
// Every face is linked in; this is the pool and the dwell time. With
// ROTATE_RANDOM the next face is drawn at random from the others, and the
// boot face is random too. Without it they run in this order. Set ROTATE_MS
// to 0 to never switch.
// The preview envs pass -D FORCE_FACE=$PREVIEW_FACE. If the variable was not
// set the macro is defined but empty; catch that with a readable error rather
// than the "expected primary-expression" the compiler would give.
#define FF_CAT_(a, b) a##b
#define FF_CAT(a, b)  FF_CAT_(a, b)
#if defined(FORCE_FACE) && FF_CAT(FORCE_FACE, 1) == 1
#error "FORCE_FACE is empty: set PREVIEW_FACE=FACE_<NAME> before building a preview env"
#endif

static const FaceVTable *const ROTATION[] = {
#if defined(FORCE_FACE)
    // Single-face preview build: -D FORCE_FACE=FACE_PULSAR (etc) pins the
    // display to that face so it can be looked at without waiting for the
    // rotation to reach it.
    &FORCE_FACE,
#elif defined(FORCE_DAY) || defined(FORCE_NIGHT)
    // Theme preview builds: only the face whose theme is being checked.
    // Otherwise the preview would start on the word clock and not reach
    // retro for three hours.
    &FACE_RETRO,
#else
    &FACE_WORD, &FACE_RETRO, &FACE_DOTMATRIX, &FACE_PULSAR, &FACE_PCB,
    &FACE_CASIO, &FACE_MOSAIC, &FACE_DEFAULT, &FACE_CLASSIC, &FACE_MODERN,
    &FACE_PANEL, &FACE_DELOREAN,
#endif
};
static const int      ROTATION_N = sizeof(ROTATION) / sizeof(ROTATION[0]);
static const uint32_t ROTATE_MS  = 3UL * 60UL * 60UL * 1000UL;   // 3 hours
static const bool     ROTATE_RANDOM = true;

const FaceVTable *activeFace = ROTATION[0];
static int      faceIdx      = 0;
static uint32_t faceSince    = 0;

// esp_random() is the hardware RNG, so nothing needs seeding. Picking from
// "everyone but the current one" guarantees each switch is visible.
static int firstFaceIdx()
{
    return (ROTATE_RANDOM && ROTATION_N > 1) ? (int)(esp_random() % ROTATION_N) : 0;
}
static int nextFaceIdx(int cur)
{
    if (ROTATION_N < 2)  return 0;
    if (!ROTATE_RANDOM)  return (cur + 1) % ROTATION_N;
    return (cur + 1 + (int)(esp_random() % (ROTATION_N - 1))) % ROTATION_N;
}
// File scope rather than function-local: a face switch resets it to force
// an immediate repaint.
static int      lastSec      = -1;

bool     timeValid = false;
uint16_t statusCol = 0xFD20;           // amber until WiFi + NTP are both good
static uint32_t lastSyncCheck = 0;

// Weather state, written by the background task and read by the render loop.
// 32-bit aligned scalars, so plain reads/writes are atomic enough here.
volatile bool wxValid = false;
volatile int  wxTempF = 0;
volatile int  wxCode  = -1;
volatile bool wxIsDay = true;
volatile int  wxErr   = 0;    // 0 ok, else the stage that failed (shown on the face)
volatile int  wxSunrise = -1; // minutes since local midnight, -1 = unknown
volatile int  wxSunset  = -1;
bool          wxUseF    = true;

// "2026-09-01T07:04" -> minutes since midnight, or -1 if it does not parse.
static int isoToMinutes(const char *iso)
{
    if (!iso) return -1;
    const char *t = strchr(iso, 'T');
    if (!t || strlen(t) < 6) return -1;
    if (!isdigit((int)t[1]) || !isdigit((int)t[2]) ||
        !isdigit((int)t[4]) || !isdigit((int)t[5])) return -1;
    int hh = (t[1] - '0') * 10 + (t[2] - '0');
    int mm = (t[4] - '0') * 10 + (t[5] - '0');
    if (hh > 23 || mm > 59) return -1;
    return hh * 60 + mm;
}

static String urlEncode(const String &s)
{
    String o;
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') o += c;
        else if (c == ' ') o += '+';
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", (unsigned char)c); o += b; }
    }
    return o;
}

// Turn the typed location into coordinates and a timezone, once. Open-Meteo's
// geocoder takes a postcode or a town name and answers over plain HTTP.
static bool resolveLocation()
{
    if (settings.hasLoc) return true;
    if (!settings.locQuery.length() || WiFi.status() != WL_CONNECTED) return false;

    String url = "http://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name="
                 + urlEncode(settings.locQuery);
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;

    bool ok = false;
    if (http.GET() == HTTP_CODE_OK) {
        // The reply lists every postcode in the town; keep only what we use.
        JsonDocument filter;
        JsonObject f = filter["results"][0].to<JsonObject>();
        f["latitude"] = true; f["longitude"] = true; f["timezone"] = true;
        f["name"] = true;     f["country_code"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter))) {
            JsonObject r = doc["results"][0];
            if (!r.isNull()) {
                settings.lat     = r["latitude"]  | 0.0f;
                settings.lon     = r["longitude"] | 0.0f;
                settings.tzName  = (const char *)(r["timezone"] | "");
                settings.locName = String((const char *)(r["name"] | "")) + ", "
                                 + (const char *)(r["country_code"] | "");
                settings.hasLoc  = true;
                settingsSave();
                ok = true;
            }
        }
    }
    http.end();
    return ok;
}

// Runs on core 0 alongside WiFi (on the S3; the C3 and C6 have one core), so
// the fetch never stalls the render loop.
//
// Plain HTTP on purpose. Open-Meteo serves this over http with no redirect,
// and a TLS handshake wants roughly 40 KB of heap plus large contiguous
// mbedTLS buffers - on top of the frame buffers and the WiFi stack, that
// allocation is what was failing.
//
// wxErr records which stage failed so the face can show it; there is no
// usable serial console on these boards.
static void weatherTask(void *)
{
    for (;;) {
        bool ok = false;

        if (WiFi.status() != WL_CONNECTED) {
            wxErr = 1;
        } else if (!settings.hasLoc && !resolveLocation()) {
            wxErr = 6;                                    // location not found
        } else {
            if (settings.tzName.length()) applyTimezone(CFG_TZ);
            wxUseF = settings.useF;

            char url[256];
            snprintf(url, sizeof url,
                     "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                     "&current=temperature_2m,weather_code,is_day"
                     "&daily=sunrise,sunset&timezone=auto&forecast_days=1"
                     "&temperature_unit=%s",
                     settings.lat, settings.lon, settings.useF ? "fahrenheit" : "celsius");

            WiFiClient client;
            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(8000);

            if (!http.begin(client, url)) {
                wxErr = 2;
            } else {
                int code = http.GET();
                if (code != HTTP_CODE_OK) {
                    wxErr = 3;
                } else {
                    JsonDocument doc;
                    if (deserializeJson(doc, http.getString())) {
                        wxErr = 4;
                    } else {
                        JsonObject cur = doc["current"];
                        if (cur.isNull()) {
                            wxErr = 5;
                        } else {
                            wxTempF = (int)lroundf(cur["temperature_2m"] | 0.0f);
                            wxCode  = cur["weather_code"] | -1;
                            wxIsDay = ((int)(cur["is_day"] | 1)) != 0;
                            wxValid = true;
                            wxErr   = 0;
                            ok      = true;

                            // timezone=auto, so these arrive already local
                            JsonObject daily = doc["daily"];
                            if (!daily.isNull()) {
                                wxSunrise = isoToMinutes(
                                    daily["sunrise"][0] | (const char *)nullptr);
                                wxSunset  = isoToMinutes(
                                    daily["sunset"][0]  | (const char *)nullptr);
                            }

                            // The feed also says which zone the coordinates
                            // are in and the current offset. That keeps the
                            // clock right for places the table does not know
                            // and across DST changes.
                            int         off = doc["utc_offset_seconds"] | 0;
                            const char *tzn = doc["timezone"] | "";
                            bool changed = !settings.hasOffset || off != settings.utcOffset
                                        || (tzn[0] && settings.tzName != tzn);
                            if (changed) {
                                settings.utcOffset = off;
                                settings.hasOffset = true;
                                if (tzn[0]) settings.tzName = tzn;
                                settingsSave();
                                applyTimezone(CFG_TZ);
                            }
                        }
                    }
                }
                http.end();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ok ? WX_PERIOD_MS : WX_RETRY_MS));
    }
}

// Park the onboard LED so it does not distract. The boards differ:
//
//   S3 Super Mini - a WS2812 on GP48, sharing the pin with a plain blue LED.
//     A floating data line makes it latch noise and flash, so clock it an
//     explicit "off" frame, then hold the pin low: the WS2812's reset state.
//
//   C3 Super Mini - a plain LED on GP8, wired active-low, so park it high.
//     GP48 does not exist on the C3, whose GPIOs stop at 21.
//
//   Waveshare C6 - no user LED on a GPIO.
static void parkOnboardLed()
{
#if CONFIG_IDF_TARGET_ESP32C6
    // nothing to park
#elif CONFIG_IDF_TARGET_ESP32C3
    pinMode(8, OUTPUT);
    digitalWrite(8, HIGH);
#else
    neopixelWrite(48, 0, 0, 0);
    pinMode(48, OUTPUT);
    digitalWrite(48, LOW);
#endif
}

// Centred boot/status message. Static screens go through screenPaint() so
// the band renderer on the AMOLED can draw them piecewise.
static const char *bannerL1, *bannerL2;
static uint16_t    bannerCol;
static void paintBanner(GfxDirect &g)
{
    uint16_t bg = activeFace->background();
    g.fillScreen(bg);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(bannerCol, bg);
    g.drawString(bannerL1, 120, 108, 4);
    if (bannerL2) {
        g.setTextColor(0x7BEF, bg);
        g.drawString(bannerL2, 120, 138, 2);
    }
}
static void banner(const char *line1, const char *line2, uint16_t color)
{
    bannerL1 = line1; bannerL2 = line2; bannerCol = color;
    screenPaint(paintBanner);
}

// What to do while the hotspot is up: the network to join, the address to
// open, and how it is going.
static const char *setupStatus = "";
static void paintSetup(GfxDirect &g)
{
    g.fillScreen(0x0000);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(0xFD20, 0x0000); g.drawString("WiFi setup",       120,  50, 4);
    g.setTextColor(0x9CD3, 0x0000); g.drawString("join the network", 120,  82, 2);
    g.setTextColor(0xFFFF, 0x0000); g.drawString(portal::AP_SSID,    120, 106, 4);
    g.setTextColor(0x9CD3, 0x0000); g.drawString("then open",        120, 134, 2);
    g.setTextColor(0xFFFF, 0x0000); g.drawString("192.168.4.1",      120, 158, 4);
    g.setTextColor(0x7BEF, 0x0000); g.drawString(setupStatus,        120, 196, 2);
}
static void drawSetupScreen(const char *status)
{
    setupStatus = status;
    screenPaint(paintSetup);
}

static const char *portalStatusText(portal::State s)
{
    switch (s) {
        case portal::CONNECTING: return "connecting...";
        case portal::OK:         return "connected";
        case portal::FAIL:       return "could not join, try again";
        default:                 return "waiting for a phone";
    }
}

// Boot-time setup: hotspot up, sit here until a network works.
static void runPortalUntilConnected()
{
    portal::start();
    portal::State shown = portal::FAIL;   // anything but IDLE, so the first pass draws
    for (;;) {
        portal::handle();
        portal::State s = portal::state();
        if (s != shown) { shown = s; drawSetupScreen(portalStatusText(s)); }
        // leave the hotspot up a few seconds so the phone can read "connected"
        if (s == portal::OK && portal::sinceState() > 4000) { portal::stop(); return; }
        delay(10);
    }
}

// Hold BOOT for three seconds at power-on to start over.
static bool bootHeld()
{
    pinMode(BOOT_BTN, INPUT_PULLUP);
    delay(20);
    if (digitalRead(BOOT_BTN) != LOW) return false;
    banner("Hold BOOT", "3 s to reset the WiFi setup", 0xFD20);
    uint32_t t0 = millis();
    while (digitalRead(BOOT_BTN) == LOW) {
        if (millis() - t0 > 3000) return true;
        delay(20);
    }
    return false;
}

// config.h values, for a chip with nothing saved.
static void seedDefaults()
{
    if (!settings.hasWifi() && strlen(WIFI_SSID)) {
        settings.ssid = WIFI_SSID;
        settings.pass = WIFI_PASS;
    }
    if (!settings.hasLoc && !settings.locQuery.length()) {
#if defined(WX_LAT) && defined(WX_LON)
        settings.lat    = atof(WX_LAT);
        settings.lon    = atof(WX_LON);
        settings.hasLoc = true;
#endif
        if (strlen(WX_LOCATION)) settings.locQuery = WX_LOCATION;
        settings.useF = strcmp(WX_UNITS, "celsius") != 0;
    }
}

// Switch to a face: clear the panel (faces do not all paint every pixel, so
// the old one would show through in the margins) and force a repaint.
static void showFace(int idx)
{
    faceIdx    = idx;
    activeFace = ROTATION[faceIdx];
    faceSince  = millis();
    lastSec    = -1;
    screenInvalidate();
    screenClear(activeFace->background());
}

void setup()
{
    // Serial is UART0 here (ARDUINO_USB_CDC_ON_BOOT=0), not USB-CDC, so
    // writes go to unconnected pins and can never block the render loop.
    Serial.begin(115200);

    parkOnboardLed();

    faceIdx    = firstFaceIdx();
    activeFace = ROTATION[faceIdx];

    // Frame buffers are claimed in here, before WiFi comes up, while the heap
    // is still unfragmented.
    screenInit();
    screenClear(activeFace->background());
#ifdef AMOLED_C6
    touch::begin();
#endif

    // Initialise every face, not just the first: rotation can reach any of
    // them, and their init work (geometry tables, tile seeds) is one-off.
    for (int i = 0; i < ROTATION_N; i++) ROTATION[i]->init();
    faceSince = millis();

    settingsLoad();
    bool startOver = bootHeld();
    if (startOver) settingsClear();
    else           seedDefaults();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    bool up = false;
    if (!startOver && settings.hasWifi()) {
        banner("WiFi", settings.ssid.c_str(), 0xE71C);
        WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) delay(250);
        up = WiFi.status() == WL_CONNECTED;
    }
    if (!up) runPortalUntilConnected();     // returns only once a network works

    // Where are we? Needed before NTP so the clock shows local time from the
    // first frame. If it fails here the weather task keeps trying.
    if (!settings.hasLoc && settings.locQuery.length()) {
        banner("Location", settings.locQuery.c_str(), 0xE71C);
        resolveLocation();
    }
    wxUseF = settings.useF;

    banner("NTP", "syncing...", 0xE71C);

    // SNTP; the IDF layer re-syncs on its own roughly hourly afterwards.
    configTzTime(tzString(CFG_TZ), "pool.ntp.org", "time.google.com", "time.nist.gov");

    uint32_t t0 = millis();
    while (time(nullptr) < 1700000000 && millis() - t0 < 20000) delay(200);
    timeValid = time(nullptr) > 1700000000;

    // Weather runs on core 0 with WiFi; the render loop owns core 1 on the
    // S3. On the single-core chips it is just another task.
    xTaskCreatePinnedToCore(weatherTask, "weather", 10240, nullptr, 1, nullptr, 0);

    screenInvalidate();
    screenClear(activeFace->background());
}

void loop()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    time_t   now = tv.tv_sec;
    struct tm t;
    localtime_r(&now, &t);

    portal::handle();

#ifdef AMOLED_C6
    // Touch: swipe through the faces in list order, hold to open setup.
    switch (touch::poll()) {
        case touch::SWIPE_LEFT:  showFace((faceIdx + 1) % ROTATION_N);              break;
        case touch::SWIPE_RIGHT: showFace((faceIdx + ROTATION_N - 1) % ROTATION_N); break;
        case touch::LONG_PRESS:  if (!portal::running()) portal::start();           break;
        default: break;
    }
#endif

    // Rotate on schedule.
    if (ROTATE_MS && millis() - faceSince >= ROTATE_MS) showFace(nextFaceIdx(faceIdx));

    // A segmented face only changes when the second does, and its angled
    // cells cost real time to rasterise - so redraw it at 1 Hz rather than
    // flat out. An animated face redraws for a smooth sweep: flat out where
    // there is a full frame buffer, at the back end's chosen rate where the
    // sweep is done with dirty boxes.
    const bool smooth = activeFace->smooth();
    const int  hz     = screenSweepHz();
    static uint32_t lastFrameMs = 0;
    bool due = smooth ? (hz == 0 || millis() - lastFrameMs >= (uint32_t)(1000 / hz))
                      : t.tm_sec != lastSec;

    if (due) {
        lastSec     = t.tm_sec;
        lastFrameMs = millis();
        float sub = smooth ? tv.tv_usec / 1000000.0f : 0.0f;
        screenRenderFace(activeFace, t, sub, portal::running());
    }

    // Re-evaluate link/sync state once a second for the status indicator.
    if (millis() - lastSyncCheck > 1000) {
        lastSyncCheck = millis();
        bool up = WiFi.status() == WL_CONNECTED;
        if (!timeValid && time(nullptr) > 1700000000) timeValid = true;
        statusCol = (timeValid && up) ? 0x2E68 : 0xFD20;

        // Self-lit panels: ease off after dark. No-op where there is a backlight.
        static int lastNight = -1;
        int night = isNightNow(t) ? 1 : 0;
        if (night != lastNight) { lastNight = night; screenSetBrightness(night ? 110 : 255); }

        // Network gone for a while: open the hotspot so new details can be
        // entered, but keep showing the time. Close it again once something
        // works and the phone has had a moment to see that.
        static uint32_t downSince = 0;
        if (up) downSince = 0;
        else if (!downSince) downSince = millis();
        if (!up && downSince && millis() - downSince > PORTAL_AFTER_DOWN_MS
            && !portal::running())
            portal::start();
        if (portal::running() && up && portal::state() != portal::CONNECTING
            && portal::sinceState() > 4000)
            portal::stop();

        // Throttled: reconnect() is a heavy call, do not hammer it at 1 Hz.
        // Not while the hotspot is up either; it would fight a fresh attempt.
        static uint32_t lastRetry = 0;
        if (!up && !portal::running() && millis() - lastRetry > 10000) {
            lastRetry = millis();
            WiFi.reconnect();
        }
    }

    // Frame rate is bounded by the panel transfer, not by this delay.
    delay(5);
}
