// ---------------------------------------------------------------------------
//  NTP clock + weather  -  ESP32-S3 Super Mini + GC9A01 240x240 round SPI LCD
//
//  This file owns the hardware, WiFi, NTP and the weather fetch. The visual
//  design lives in faces/ - exactly one face is compiled per build, selected
//  by build_src_filter in platformio.ini:
//
//      pio run -e default -t upload    analog face
//      pio run -e casio   -t upload    Casio-style segmented LCD face
//
//  Rendering goes into an off-screen sprite and is blitted once per frame so
//  nothing flickers, falling back to direct-to-panel drawing if the 115 KB
//  sprite cannot be allocated.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <sys/time.h>
#include "face.h"

// ----------------------------- user settings -------------------------------
// WiFi, timezone and location live in config.h, which is gitignored. Copy
// config.example.h to config.h and fill it in.
#include "config.h"

// Weather: Open-Meteo, which needs no API key.
static const uint32_t WX_PERIOD_MS = 10UL * 60UL * 1000UL;  // refresh every 10 min
static const uint32_t WX_RETRY_MS  = 30UL * 1000UL;         // sooner after a failure

// ---------------------------------------------------------------------------

// ---- face rotation --------------------------------------------------------
// Every face is linked in; this is the running order and the dwell time.
// Set ROTATE_MS to 0 to pin the display to ROTATION[0] and never switch.
static const FaceVTable *const ROTATION[] = {
#if defined(FORCE_DAY) || defined(FORCE_NIGHT)
    // Theme preview builds: only the face whose theme is being checked.
    // Otherwise the preview would start on the word clock and not reach
    // retro for three hours.
    &FACE_RETRO,
#else
    &FACE_WORD, &FACE_RETRO, &FACE_DOTMATRIX,
    &FACE_CASIO, &FACE_MOSAIC, &FACE_DEFAULT,
#endif
};
static const int      ROTATION_N = sizeof(ROTATION) / sizeof(ROTATION[0]);
static const uint32_t ROTATE_MS  = 3UL * 60UL * 60UL * 1000UL;   // 3 hours

const FaceVTable *activeFace = ROTATION[0];
static int      faceIdx      = 0;
static uint32_t faceSince    = 0;
// File scope rather than function-local: the rotation resets it to force an
// immediate repaint when the face changes.
static int      lastSec      = -1;

static const char *WX_URL =
    "http://api.open-meteo.com/v1/forecast?latitude=" WX_LAT "&longitude=" WX_LON
    "&current=temperature_2m,weather_code,is_day"
    "&daily=sunrise,sunset&timezone=auto&forecast_days=1"
    "&temperature_unit=" WX_UNITS;

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite fb  = TFT_eSprite(&tft);   // full-screen frame buffer
bool        useSprite = false;

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

// Runs on core 0 alongside WiFi, so the fetch never stalls the render loop
// on core 1.
//
// Plain HTTP on purpose. Open-Meteo serves this endpoint over http with no
// redirect (verified), and a TLS handshake wants roughly 40 KB of heap plus
// large contiguous mbedTLS buffers - on top of the 115 KB sprite and the WiFi
// stack, that allocation is what was failing.
//
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

// wxErr records which stage failed so the face can show it; there is no
// usable serial console on this board.
static void weatherTask(void *)
{
    for (;;) {
        bool ok = false;

        if (WiFi.status() != WL_CONNECTED) {
            wxErr = 1;
        } else {
            WiFiClient client;
            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(8000);

            if (!http.begin(client, WX_URL)) {
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
                        }
                    }
                }
                http.end();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ok ? WX_PERIOD_MS : WX_RETRY_MS));
    }
}

// Park the onboard LED so it does not distract. The two boards differ:
//
//   S3 Super Mini - a WS2812 on GP48, sharing the pin with a plain blue LED.
//     A floating data line makes it latch noise and flash, so clock it an
//     explicit "off" frame, then hold the pin low: the WS2812's reset state.
//
//   C3 Super Mini - a plain LED on GP8, wired active-low, so park it high.
//     GP48 does not exist on the C3, whose GPIOs stop at 21.
static void parkOnboardLed()
{
#if CONFIG_IDF_TARGET_ESP32C3
    pinMode(8, OUTPUT);
    digitalWrite(8, HIGH);
#else
    neopixelWrite(48, 0, 0, 0);
    pinMode(48, OUTPUT);
    digitalWrite(48, LOW);
#endif
}

// Centred boot/status message, always straight to the panel.
static void banner(const char *line1, const char *line2, uint16_t color)
{
    uint16_t bg = activeFace->background();
    tft.fillScreen(bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(color, bg);
    tft.drawString(line1, 120, 108, 4);
    if (line2) {
        tft.setTextColor(0x7BEF, bg);
        tft.drawString(line2, 120, 138, 2);
    }
}

void setup()
{
    // Serial is UART0 here (ARDUINO_USB_CDC_ON_BOOT=0), not USB-CDC, so
    // writes go to unconnected pins and can never block the render loop.
    Serial.begin(115200);

    parkOnboardLed();

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(activeFace->background());

    // Initialise every face, not just the first: rotation can reach any of
    // them, and their init work (geometry tables, tile seeds) is one-off.
    for (int i = 0; i < ROTATION_N; i++) ROTATION[i]->init();
    activeFace = ROTATION[0];
    faceSince  = millis();

    // 240*240*2 = 115200 bytes, claimed before WiFi comes up so the heap is
    // still unfragmented. If it fails we still run, just slower.
    fb.setColorDepth(16);
    if (fb.createSprite(240, 240) != nullptr) {
        useSprite = true;
        Serial.println("[disp] sprite frame buffer allocated");
    } else {
        Serial.println("[disp] sprite alloc FAILED - drawing direct");
    }

    banner("WiFi", WIFI_SSID, 0xE71C);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) delay(250);

    bool up = WiFi.status() == WL_CONNECTED;
    banner(up ? "NTP" : "No WiFi", up ? "syncing..." : "retrying...",
           up ? 0xE71C : 0xFD20);

    // SNTP; the IDF layer re-syncs on its own roughly hourly afterwards.
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "time.nist.gov");

    t0 = millis();
    while (time(nullptr) < 1700000000 && millis() - t0 < 20000) delay(200);
    timeValid = time(nullptr) > 1700000000;

    // Weather runs on core 0 with WiFi; the render loop owns core 1.
    xTaskCreatePinnedToCore(weatherTask, "weather", 10240, nullptr, 1, nullptr, 0);
}

void loop()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    time_t   now = tv.tv_sec;
    struct tm t;
    localtime_r(&now, &t);

    // A segmented face only changes when the second does, and its angled
    // cells cost real time to rasterise - so redraw it at 1 Hz rather than
    // flat out. An animated face redraws every pass for a smooth sweep.
    // Rotate on schedule. Clearing the panel on the way out matters: faces do
    // not all paint every pixel, so without it the previous face can show
    // through in the margins.
    if (ROTATE_MS && millis() - faceSince >= ROTATE_MS) {
        faceIdx    = (faceIdx + 1) % ROTATION_N;
        activeFace = ROTATION[faceIdx];
        faceSince  = millis();
        lastSec    = -1;                       // force an immediate repaint
        if (useSprite) fb.fillSprite(activeFace->background());
        tft.fillScreen(activeFace->background());
    }

    const bool smooth = activeFace->smooth();

    bool due = smooth || t.tm_sec != lastSec;

    if (due) {
        lastSec = t.tm_sec;
        float sub = smooth ? tv.tv_usec / 1000000.0f : 0.0f;
        if (useSprite) {
            activeFace->renderSprite(fb, t, sub);
            fb.pushSprite(0, 0);
        } else {
            activeFace->renderDirect(tft, t, sub);
        }
    }

    // Re-evaluate link/sync state once a second for the status indicator.
    if (millis() - lastSyncCheck > 1000) {
        lastSyncCheck = millis();
        bool up = WiFi.status() == WL_CONNECTED;
        if (!timeValid && time(nullptr) > 1700000000) timeValid = true;
        statusCol = (timeValid && up) ? 0x2E68 : 0xFD20;

        // Throttled: reconnect() is a heavy call, do not hammer it at 1 Hz.
        static uint32_t lastRetry = 0;
        if (!up && millis() - lastRetry > 10000) {
            lastRetry = millis();
            WiFi.reconnect();
        }
    }

    // Frame rate is bounded by the SPI transfer, not by this delay.
    delay(5);
}
