// ---------------------------------------------------------------------------
//  Face: "default"  -  analog clock with weather
//
//  Black face, red 12/3/6/9, anti-aliased hands. The hub centre doubles as
//  the WiFi/NTP indicator. Weather icon inboard of the 9, temperature
//  inboard of the 3.
//
//  Performance note: the face is redrawn every frame, so it must stay cheap.
//  Anti-aliasing is spent only where it shows - the hands and the hour ticks.
//  A full-face fillSmoothCircle() or 60 anti-aliased ticks costs seconds per
//  frame and makes the clock look frozen.
// ---------------------------------------------------------------------------
#include "../face.h"

// Wrapped in its own namespace so every face can be linked into the same
// binary: each defines faceInit/faceRender/faceBackground/faceSmooth, which
// would otherwise be six duplicate global symbols.
namespace face_default {

#define CX      120        // face centre
#define CY      120
#define R       119        // face radius

#define ICON_X  (CX - 52)  // weather icon, inboard of the 9
#define TEMP_X  (CX + 48)  // temperature, inboard of the 3

// palette (RGB565)
#define C_FACE      0x0000   // pure black face
#define C_RING      0x4A69   // outer bezel
#define C_TICK      0x8C71   // minute ticks
#define C_TICK_HR   0xFFFF   // hour ticks
#define C_NUM       0xE71C   // minor numerals (1,2,4,5,7,8,10,11)
#define C_NUM_MAJOR 0xF800   // 12, 3, 6, 9 picked out in red
#define C_HAND      0xFFFF   // hour + minute hands
#define C_SEC       0xF9A6   // second hand (warm red)
#define C_HUB       0xFFFF
#define C_TEXT      0x7BEF   // date / status text
#define C_TEMP      0xFFFF   // temperature readout
#define C_DIRECT    0x07FF   // "running without sprite" marker

// --- precomputed geometry: 120 trig calls per frame is pure waste ----------
struct Tick { int16_t x0, y0, x1, y1; bool hour; };
static Tick  ticks[60];
struct Num  { int16_t x, y; uint8_t n; };
static Num   numerals[12];

uint16_t faceBackground() { return C_FACE; }
bool     faceSmooth()     { return true; }   // sweeping second hand

void faceInit()
{
    for (int i = 0; i < 60; i++) {
        float a = i * 6.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        bool  hr = (i % 5 == 0);
        float rOut = R - 6;
        float rIn  = hr ? R - 20 : R - 12;
        ticks[i] = { (int16_t)(CX + rIn * s),  (int16_t)(CY - rIn * c),
                     (int16_t)(CX + rOut * s), (int16_t)(CY - rOut * c), hr };
    }
    for (int n = 1; n <= 12; n++) {
        float a = n * 30.0f * DEG_TO_RAD;
        numerals[n - 1] = { (int16_t)(CX + 85 * sinf(a)),
                            (int16_t)(CY - 85 * cosf(a)), (uint8_t)n };
    }
}

template <typename GFX>
static void drawDial(GFX &g)
{
    g.drawSmoothCircle(CX, CY, R, C_RING, C_FACE);

    // Anti-alias the 12 hour ticks; the 48 minute ticks are plain lines.
    for (int i = 0; i < 60; i++) {
        const Tick &t = ticks[i];
        if (t.hour) g.drawWideLine(t.x0, t.y0, t.x1, t.y1, 3.0f, C_TICK_HR, C_FACE);
        else        g.drawLine(t.x0, t.y0, t.x1, t.y1, C_TICK);
    }

    g.setTextDatum(MC_DATUM);
    for (int i = 0; i < 12; i++) {
        uint8_t n = numerals[i].n;
        // n % 3 == 0 picks out exactly 3, 6, 9 and 12
        g.setTextColor((n % 3 == 0) ? C_NUM_MAJOR : C_NUM, C_FACE);
        g.drawNumber(n, numerals[i].x, numerals[i].y, 4);
    }
}

template <typename GFX>
static void drawWeather(GFX &g)
{
    wxIconColor(g, ICON_X, CY, wxValid ? iconForCode(wxCode) : WX_UNKNOWN,
                wxIsDay, C_FACE);

    // temperature with a drawn degree ring, because the built-in fonts stop
    // at ASCII 126 and have no degree glyph
    char buf[8];
    if (wxValid) snprintf(buf, sizeof(buf), "%d", wxTempF);
    else         strcpy(buf, "--");

    g.setTextColor(C_TEMP, C_FACE);
    int tw   = g.textWidth(buf, 4);
    int left = TEMP_X - (tw + 11) / 2;

    g.setTextDatum(ML_DATUM);
    g.drawString(buf, left, CY, 4);
    if (wxValid) {
        g.drawCircle(left + tw + 3, CY - 7, 2, C_TEMP);
        g.drawString("F", left + tw + 7, CY, 2);
    }
}

template <typename GFX>
static void drawText(GFX &g, const struct tm &t)
{
    char buf[32];
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_TEXT, C_FACE);

    strftime(buf, sizeof(buf), "%a %d %b", &t);
    g.drawString(buf, CX, CY - 46, 2);

    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    g.drawString(buf, CX, CY + 50, 2);

    // cyan dot = sprite alloc failed, running direct (slow)
    if (!useSprite) g.fillSmoothCircle(CX, CY + 72, 3, C_DIRECT, C_FACE);
}

template <typename GFX>
static void drawHands(GFX &g, float h, float m, float s)
{
    float ah = h * 30.0f * DEG_TO_RAD;     // 30 deg per hour
    float am = m *  6.0f * DEG_TO_RAD;     //  6 deg per minute
    float as = s *  6.0f * DEG_TO_RAD;     //  6 deg per second

    g.drawWideLine(CX - 12 * sinf(ah), CY + 12 * cosf(ah),
                   CX + 52 * sinf(ah), CY - 52 * cosf(ah),
                   7.0f, C_HAND, C_FACE);

    g.drawWideLine(CX - 16 * sinf(am), CY + 16 * cosf(am),
                   CX + 82 * sinf(am), CY - 82 * cosf(am),
                   5.0f, C_HAND, C_FACE);

    g.drawWideLine(CX - 26 * sinf(as), CY + 26 * cosf(as),
                   CX + 96 * sinf(as), CY - 96 * cosf(as),
                   2.0f, C_SEC, C_FACE);
    g.fillSmoothCircle(CX - 26 * sinf(as), CY + 26 * cosf(as), 5, C_SEC, C_FACE);

    // Hub. Its centre doubles as the WiFi/NTP indicator.
    g.fillSmoothCircle(CX, CY, 7, C_HUB, C_FACE);
    g.fillSmoothCircle(CX, CY, 4, statusCol, C_HUB);
}

template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    float s = t.tm_sec + subSec;
    float m = t.tm_min + s / 60.0f;
    float h = (t.tm_hour % 12) + m / 60.0f;

    // The panel is round, so the corners are never visible and a flat fill is
    // fine - far cheaper than an anti-aliased full-face circle.
    g.fillScreen(C_FACE);

    drawDial(g);
    drawWeather(g);
    drawText(g, t);
    drawHands(g, h, m, s);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_default

// Registration: the one symbol this file exposes.
const FaceVTable FACE_DEFAULT = {
    "default",
    face_default::faceInit,
    face_default::faceBackground,
    face_default::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_default::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_default::faceRender,
};
