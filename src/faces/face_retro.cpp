// ---------------------------------------------------------------------------
//  Face: "retro"  -  green-LCD digital watch
//
//  Black dial with a single pale mint LCD strip across the middle carrying the
//  time, and supporting rows above and below it in pale green on black:
//
//      01 SEP 2026                     date
//      SUN MON [TUE] WED THU FRI SAT   week, today inverted
//      [ sig |  10:57  | SEC 42 ]      the LCD strip
//      SUNRISE:07:04 TO SUNSET:19:47   from the weather feed, real values
//      (icon)  84 F                    condition + temperature
//
//  24-hour time, as every digital face in this project is.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <string.h>

// Wrapped in its own namespace so every face can be linked into the same
// binary: each defines faceInit/faceRender/faceBackground/faceSmooth, which
// would otherwise be six duplicate global symbols.
namespace face_retro {

// ---- palette --------------------------------------------------------------
// By day the strip is a plain grey LCD. After sunset the electroluminescent
// backlight comes on and the substrate glows mint green - the segments stay
// dark either way, because an EL panel lights the background, not the digits.
// The switch is deliberately instant: a backlight snaps on, it does not fade.
#define C_BG      0x0000   // dial
#define C_RING    0x2124   // faint bezel ring
#define C_WARN    0xFD20   // clock not synced

struct Theme {
    uint16_t panel;   // LCD substrate
    uint16_t ink;     // segments and text on the strip
    uint16_t text;    // text out on the dial
    uint16_t dim;     // inactive week days
};
// Daytime substrate is the classic Casio LCD grey-green, ~RGB(205,206,189):
// a pale warm grey with a faint green cast, not a blue-grey.
static const Theme DAY_T   = { 0xCE77, 0x1082, 0xC618, 0x738E };   // Casio LCD
// EL backlight green, ~RGB(90,230,172): saturated blue-green, the way an
// Electro Luminescence panel actually glows. The earlier pale mint sat only
// 44 RGB units from the daytime grey-green, which read as no change at all.
static const Theme NIGHT_T = { 0x5F35, 0x0841, 0x9F7B, 0x3DEC };   // EL green

// The shared isNightNow() already switches on real sunrise/sunset, which is
// what keeps this backlight consistent with the times printed on the strip.
// -D FORCE_DAY or -D FORCE_NIGHT pins the theme so either look can be checked
// without waiting for the sun. Neither is set in a normal build.
static bool isNightHere(const struct tm &t)
{
#if defined(FORCE_DAY)
    (void)t;
    return false;
#elif defined(FORCE_NIGHT)
    (void)t;
    return true;
#else
    return isNightNow(t);
#endif
}

// ---- LCD strip geometry ---------------------------------------------------
#define LCD_X   16
#define LCD_Y   84
#define LCD_W   208
#define LCD_H   58

// row baselines
#define Y_DATE   38
#define Y_WEEK   64
#define Y_SUN   154
#define Y_TEMP  174

uint16_t faceBackground() { return C_BG; }
bool     faceSmooth()     { return false; }   // digital: redraw on the second
void     faceInit()       { }

// ---- 7-segment renderer ---------------------------------------------------
//   bit: a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40
static const uint8_t SEG_MAP[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

template <typename GFX>
static void seg7(GFX &g, int x, int y, int w, int h, int t, int d, uint16_t on)
{
    if (d < 0 || d > 9) return;
    uint8_t m    = SEG_MAP[d];
    int     half = (h - t) / 2;
    int     vh   = (h - 3 * t) / 2;
    if (vh < 1) vh = 1;
    if (m & 0x01) g.fillRect(x + t,     y,            w - 2 * t, t,  on);
    if (m & 0x20) g.fillRect(x,         y + t,        t,         vh, on);
    if (m & 0x02) g.fillRect(x + w - t, y + t,        t,         vh, on);
    if (m & 0x40) g.fillRect(x + t,     y + half,     w - 2 * t, t,  on);
    if (m & 0x10) g.fillRect(x,         y + half + t, t,         vh, on);
    if (m & 0x04) g.fillRect(x + w - t, y + half + t, t,         vh, on);
    if (m & 0x08) g.fillRect(x + t,     y + h - t,    w - 2 * t, t,  on);
}

template <typename GFX>
static void seg7Pair(GFX &g, int x, int y, int w, int h, int t, int gap,
                     int v, uint16_t on)
{
    seg7(g, x,           y, w, h, t, (v / 10) % 10, on);
    seg7(g, x + w + gap, y, w, h, t, v % 10,        on);
}

static const char *DAYS[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char *MONS[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;
    char buf[40];
    const Theme &th = isNightHere(t) ? NIGHT_T : DAY_T;

    g.fillScreen(C_BG);
    g.drawSmoothCircle(120, 120, 117, C_RING, C_BG);

    // ---- date ------------------------------------------------------------
    // Font 1 doubled: blocky and monospaced, which is what the reference's
    // pixel font reads as. Proportional fonts lose that entirely.
    snprintf(buf, sizeof(buf), "%02d %s %04d",
             t.tm_mday, MONS[t.tm_mon % 12], t.tm_year + 1900);
    g.setTextSize(2);
    g.setTextDatum(TC_DATUM);
    g.setTextColor(th.text, C_BG);
    g.drawString(buf, 120, Y_DATE, 1);
    g.setTextSize(1);

    // ---- week strip, today inverted --------------------------------------
    {
        const int cw = 24;                       // 3 chars at font 1 + padding
        const int total = 7 * cw;
        int x = 120 - total / 2;
        g.setTextDatum(TC_DATUM);
        for (int d = 0; d < 7; d++) {
            bool today = (d == t.tm_wday % 7);
            if (today) {
                g.fillRoundRect(x + d * cw + 1, Y_WEEK - 3, cw - 2, 14, 3, th.text);
                g.setTextColor(C_BG, th.text);
            } else {
                g.setTextColor(th.dim, C_BG);
            }
            g.drawString(DAYS[d], x + d * cw + cw / 2, Y_WEEK, 1);
        }
    }

    // ---- the LCD strip ---------------------------------------------------
    g.fillRoundRect(LCD_X, LCD_Y, LCD_W, LCD_H, 6, th.panel);

    // Weather icon in the strip's left slot, drawn mono in the segment ink:
    // a full-colour icon washes out against a light LCD substrate, and dark
    // marks on a lit background is what a real segment panel does anyway.
    wxIconMono(g, LCD_X + 24, LCD_Y + LCD_H / 2,
               wxValid ? iconForCode(wxCode) : WX_UNKNOWN, wxIsDay,
               th.ink, th.panel);

    {   // 24-hour time, the strip's whole reason for existing
        const int dw = 26, dh = 40, dt = 6, dg = 4, cw = 6;
        int x  = LCD_X + 46;
        int dy = LCD_Y + 9;
        uint16_t ink = timeValid ? th.ink : C_WARN;

        seg7Pair(g, x, dy, dw, dh, dt, dg, t.tm_hour, ink);
        int cx = x + 2 * dw + 2 * dg;
        g.fillRect(cx, dy + 10,      cw, cw, ink);
        g.fillRect(cx, dy + dh - 16, cw, cw, ink);
        seg7Pair(g, cx + cw + dg, dy, dw, dh, dt, dg, t.tm_min, ink);
    }

    {   // seconds, in the slot the reference used for heart rate
        g.setTextDatum(TR_DATUM);
        g.setTextColor(th.ink, th.panel);
        g.drawString("SEC", LCD_X + LCD_W - 8, LCD_Y + 10, 1);
        seg7Pair(g, LCD_X + LCD_W - 30, LCD_Y + 26, 10, 18, 3, 2, t.tm_sec, th.ink);
    }

    // ---- sunrise / sunset ------------------------------------------------
    if (wxSunrise >= 0 && wxSunset >= 0)
        snprintf(buf, sizeof(buf), "SUNRISE:%02d:%02d TO SUNSET:%02d:%02d",
                 wxSunrise / 60, wxSunrise % 60, wxSunset / 60, wxSunset % 60);
    else
        strcpy(buf, "SUNRISE:--:-- TO SUNSET:--:--");
    g.setTextDatum(TC_DATUM);
    g.setTextColor(th.text, C_BG);
    g.drawString(buf, 120, Y_SUN, 1);

    // ---- temperature -----------------------------------------------------
    // Alone on the bottom row now that the icon has moved up, so it gets the
    // full width and a larger size. Font 1 at size 3 is exactly 18 px per
    // character, so the group measures directly.
    {
        char tmp[10];
        if (wxValid) snprintf(tmp, sizeof(tmp), "%d", wxTempF);
        else         strcpy(tmp, "--");

        int numW  = (int)strlen(tmp) * 18;
        int total = numW + 22;                   // number + degree ring + F
        int left  = 120 - total / 2;

        g.setTextSize(3);
        g.setTextDatum(TL_DATUM);
        g.setTextColor(th.text, C_BG);
        g.drawString(tmp, left, Y_TEMP, 1);
        g.setTextSize(1);

        int nx = left + numW + 5;
        g.drawCircle(nx + 3, Y_TEMP + 5, 3, th.text);
        g.drawString("F", nx + 9, Y_TEMP + 8, 2);
    }
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_retro

// Registration: the one symbol this file exposes.
const FaceVTable FACE_RETRO = {
    "retro",
    face_retro::faceInit,
    face_retro::faceBackground,
    face_retro::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_retro::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_retro::faceRender,
};
