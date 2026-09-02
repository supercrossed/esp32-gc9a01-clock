// ---------------------------------------------------------------------------
//  Watch face interface + shared drawing helpers.
//
//  main.cpp owns the hardware, WiFi, NTP and the weather fetch. Exactly one
//  file in faces/ is compiled into a given build (selected by build_src_filter
//  in platformio.ini) and implements the four entry points below.
//
//  Faces are rendered through a template so the same code serves both the
//  buffered path (TFT_eSprite) and the direct-to-panel fallback (TFT_eSPI);
//  each face exposes that as two concrete overloads.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <math.h>

// ---- shared drawing surfaces (owned by main.cpp) --------------------------
extern TFT_eSPI    tft;
extern TFT_eSprite fb;
extern bool        useSprite;

// ---- shared live state (owned by main.cpp) -------------------------------
extern volatile bool wxValid;    // a weather fetch has succeeded at least once
extern volatile int  wxTempF;
extern volatile int  wxCode;     // WMO code
extern volatile bool wxIsDay;    // Open-Meteo is_day flag
extern volatile int  wxSunrise;  // minutes since local midnight, -1 unknown
extern volatile int  wxSunset;
extern volatile int  wxErr;      // 0 = ok, else which fetch stage failed:
                                 // 1 WiFi down  2 begin  3 HTTP  4 JSON  5 shape
extern uint16_t      statusCol;  // green = WiFi up + NTP synced, amber otherwise
extern bool          timeValid;

// ---- the face interface --------------------------------------------------
// Every face lives in its own namespace and exposes one FaceVTable, so all of
// them can be linked into a single binary and selected at runtime. Without the
// namespaces the six copies of faceInit/faceRender/... would collide.
struct FaceVTable {
    const char *name;
    void      (*init)();
    uint16_t  (*background)();
    bool      (*smooth)();                 // true = animates continuously
    void      (*renderSprite)(TFT_eSprite &, const struct tm &, float);
    void      (*renderDirect)(TFT_eSPI    &, const struct tm &, float);
};

// One per faces/*.cpp.
extern const FaceVTable FACE_DEFAULT, FACE_CASIO, FACE_MOSAIC,
                        FACE_RETRO, FACE_DOTMATRIX, FACE_WORD;

// The active face, and how long each is shown before rotating.
extern const FaceVTable *activeFace;

// ---- day / night ---------------------------------------------------------
// One answer for every face, so they cannot disagree about whether it is
// night. Prefer the real sunrise/sunset from the weather feed - the retro
// face prints those times, and its backlight has to agree with them. Fall
// back to Open-Meteo's is_day flag, and before the first fetch, to the clock.
inline bool isNightNow(const struct tm &t)
{
    if (wxSunrise >= 0 && wxSunset >= 0) {
        int now = t.tm_hour * 60 + t.tm_min;
        return now < wxSunrise || now >= wxSunset;
    }
    if (wxValid) return !wxIsDay;
    return (t.tm_hour < 7 || t.tm_hour >= 19);
}

// ---- weather icons, shared by all faces ----------------------------------
enum WxIcon { WX_UNKNOWN, WX_CLEAR, WX_PARTLY, WX_CLOUD, WX_FOG,
              WX_RAIN, WX_SNOW, WX_STORM };

inline WxIcon iconForCode(int code)
{
    switch (code) {
        case 0:                                     return WX_CLEAR;
        case 1: case 2:                             return WX_PARTLY;
        case 3:                                     return WX_CLOUD;
        case 45: case 48:                           return WX_FOG;
        case 51: case 53: case 55: case 56: case 57:
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82:                  return WX_RAIN;
        case 71: case 73: case 75: case 77:
        case 85: case 86:                           return WX_SNOW;
        case 95: case 96: case 99:                  return WX_STORM;
        default:                                    return WX_UNKNOWN;
    }
}

// Small vector glyphs. Cheap primitives only - these redraw every frame.
template <typename GFX>
inline void wxSun(GFX &g, int x, int y, int r, uint16_t c)
{
    g.fillCircle(x, y, r, c);
    for (int i = 0; i < 8; i++) {
        float a = i * 45.0f * DEG_TO_RAD;
        float s = sinf(a), co = cosf(a);
        g.drawLine(x + (r + 2) * s, y - (r + 2) * co,
                   x + (r + 5) * s, y - (r + 5) * co, c);
    }
}

template <typename GFX>
inline void wxMoon(GFX &g, int x, int y, int r, uint16_t c, uint16_t bg)
{
    g.fillCircle(x, y, r, c);
    g.fillCircle(x + r / 2, y - r / 3, r, bg);   // carve the crescent
}

// `grow` inflates the shape, which lets a caller stamp a slightly larger
// background copy first to punch a clean gap between overlapping glyphs.
template <typename GFX>
inline void wxCloud(GFX &g, int x, int y, uint16_t c, int grow = 0)
{
    g.fillCircle(x - 6, y + 2, 5 + grow, c);
    g.fillCircle(x + 6, y + 2, 5 + grow, c);
    g.fillCircle(x,     y - 2, 7 + grow, c);
    g.fillRect(x - 6 - grow, y + 2 - grow, 12 + 2 * grow, 5 + 2 * grow, c);
}

// Full-colour icon, used by the analog face.
template <typename GFX>
inline void wxIconColor(GFX &g, int x, int y, WxIcon ic, bool day, uint16_t bg)
{
    const uint16_t SUN = 0xFDA0, MOON = 0xDEFB, CLOUD = 0xBDF7;
    const uint16_t RAIN = 0x5D9F, SNOW = 0xFFFF, FOG = 0x8C71, TXT = 0x7BEF;

    switch (ic) {
        case WX_CLEAR:
            if (day) wxSun(g, x, y, 8, SUN);
            else     wxMoon(g, x, y, 9, MOON, bg);
            break;
        case WX_PARTLY:
            if (day) wxSun(g, x - 4, y - 6, 6, SUN);
            else     wxMoon(g, x - 4, y - 6, 6, MOON, bg);
            wxCloud(g, x + 2, y + 4, bg, 2);
            wxCloud(g, x + 2, y + 4, CLOUD);
            break;
        case WX_CLOUD:
            wxCloud(g, x, y, CLOUD);
            break;
        case WX_FOG:
            wxCloud(g, x, y - 4, CLOUD);
            for (int i = 0; i < 3; i++)
                g.drawFastHLine(x - 9 + (i % 2) * 4, y + 8 + i * 3, 15, FOG);
            break;
        case WX_RAIN:
            wxCloud(g, x, y - 4, CLOUD);
            for (int i = -1; i <= 1; i++)
                g.drawLine(x + i * 6, y + 6, x + i * 6 - 2, y + 12, RAIN);
            break;
        case WX_SNOW:
            wxCloud(g, x, y - 4, CLOUD);
            for (int i = -1; i <= 1; i++)
                g.fillCircle(x + i * 6, y + 9, 2, SNOW);
            break;
        case WX_STORM:
            wxCloud(g, x, y - 4, CLOUD);
            g.fillTriangle(x - 1, y + 5, x + 5, y + 5, x - 1, y + 13, SUN);
            g.fillTriangle(x + 3, y + 6, x - 3, y + 15, x + 3, y + 9,  SUN);
            break;
        default:
            g.drawCircle(x, y, 8, TXT);
            g.drawLine(x - 4, y, x + 4, y, TXT);
            break;
    }
}

// Single-colour icon for LCD-style faces, where everything is one "ink"
// colour on a panel. Overlaps are separated by stamping the background first.
template <typename GFX>
inline void wxIconMono(GFX &g, int x, int y, WxIcon ic, bool day,
                       uint16_t fg, uint16_t bg)
{
    switch (ic) {
        case WX_CLEAR:
            if (day) wxSun(g, x, y, 7, fg);
            else     wxMoon(g, x, y, 9, fg, bg);
            break;
        case WX_PARTLY:
            if (day) wxSun(g, x - 5, y - 7, 5, fg);
            else     wxMoon(g, x - 5, y - 7, 6, fg, bg);
            wxCloud(g, x + 2, y + 4, bg, 2);
            wxCloud(g, x + 2, y + 4, fg);
            break;
        case WX_CLOUD:
            wxCloud(g, x, y, fg);
            break;
        case WX_FOG:
            wxCloud(g, x, y - 4, fg);
            for (int i = 0; i < 3; i++)
                g.drawFastHLine(x - 9 + (i % 2) * 4, y + 8 + i * 3, 15, fg);
            break;
        case WX_RAIN:
            wxCloud(g, x, y - 4, fg);
            for (int i = -1; i <= 1; i++)
                g.drawLine(x + i * 6, y + 6, x + i * 6 - 2, y + 12, fg);
            break;
        case WX_SNOW:
            wxCloud(g, x, y - 4, fg);
            for (int i = -1; i <= 1; i++)
                g.fillCircle(x + i * 6, y + 9, 2, fg);
            break;
        case WX_STORM:
            wxCloud(g, x, y - 4, fg);
            g.fillTriangle(x - 2, y + 4, x + 6, y + 4, x - 2, y + 14, bg);
            g.fillTriangle(x - 1, y + 5, x + 5, y + 5, x - 1, y + 13, fg);
            break;
        default:
            g.drawCircle(x, y, 8, fg);
            g.drawLine(x - 4, y, x + 4, y, fg);
            break;
    }
}
