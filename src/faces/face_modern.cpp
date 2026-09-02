// ---------------------------------------------------------------------------
//  Face: "modern"  -  charcoal dial, baton indices, lumed hands, complications
//
//  The contemporary analog face, in the style of a current sports watch: a
//  dark dial inside a darker bezel, a dotted minute track with white baton
//  indices and a double baton at 12, broad white hands edged in black with a
//  lume strip down the middle, and an orange centre seconds hand with a
//  counterweight. Three complications: weather at 9 (icon and temperature in
//  a sub-dial), the date at 3 in a rounded tile with the weekday over it,
//  and at 6 a sun or moon for the time of day with the next sunrise or sunset
//  time beneath it.
//
//  Redrawn every frame. The dial is a plain fillCircle with one anti-aliased
//  ring on top rather than a smooth fill, which would cost seconds a frame.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <ctype.h>

namespace face_modern {

#define CX      120
#define CY      120

// palette
#define C_BEZEL     0x18C3   // outer ring
#define C_DIAL      0x0841   // charcoal
#define C_INDEX     0xFFFF   // batons
#define C_DOT       0x630C   // minute dots
#define C_ACCENT    0xFC60   // orange: second hand, weekday
#define C_LUME      0xDFFB   // lume strip: barely-green white, so the hands still read white
#define C_HAND      0xFFFF
#define C_HAND_EDGE 0x0000
#define C_TEXT      0xFFFF
#define C_TEXT2     0x9CD3
#define C_SUB       0x10A2   // sub-dial and tile fill
#define C_SUB_RING  0x4208

// geometry
#define R_DIAL     111       // inside the bezel
#define R_DOT      104
#define R_BAT_O    106
#define R_BAT_I     94
#define WX_CX       68       // weather sub-dial
#define WX_CY      120
#define WX_R        26
#define DATE_CX    170       // date tile
#define DATE_CY    120
#define DATE_S      40
#define SUN_CX     120       // day/night sub-dial
#define SUN_CY     172
#define SUN_R       22

uint16_t faceBackground() { return C_BEZEL; }
bool     faceSmooth()     { return true; }

struct Pt { int16_t x, y; };
static Pt dots[60];
struct Bat { int16_t x0, y0, x1, y1; };
static Bat batons[12];

void faceInit()
{
    for (int i = 0; i < 60; i++) {
        float a = i * 6.0f * DEG_TO_RAD;
        dots[i] = { (int16_t)lroundf(CX + R_DOT * sinf(a)),
                    (int16_t)lroundf(CY - R_DOT * cosf(a)) };
    }
    for (int i = 0; i < 12; i++) {
        float a = i * 30.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        batons[i] = { (int16_t)lroundf(CX + R_BAT_I * s), (int16_t)lroundf(CY - R_BAT_I * c),
                      (int16_t)lroundf(CX + R_BAT_O * s), (int16_t)lroundf(CY - R_BAT_O * c) };
    }
}

template <typename GFX>
static void drawDial(GFX &g)
{
    g.fillScreen(C_BEZEL);
    g.fillCircle(CX, CY, R_DIAL, C_DIAL);
    g.drawSmoothCircle(CX, CY, R_DIAL, C_SUB_RING, C_BEZEL);

    for (int i = 0; i < 60; i++)
        if (i % 5) g.fillCircle(dots[i].x, dots[i].y, 1, C_DOT);

    for (int i = 0; i < 12; i++) {
        const Bat &b = batons[i];
        if (i == 0) {
            // double baton at 12
            g.drawWideLine(b.x0 - 4, b.y0, b.x1 - 4, b.y1, 4.0f, C_INDEX, C_DIAL);
            g.drawWideLine(b.x0 + 4, b.y0, b.x1 + 4, b.y1, 4.0f, C_INDEX, C_DIAL);
        } else {
            g.drawWideLine(b.x0, b.y0, b.x1, b.y1, 4.0f, C_INDEX, C_DIAL);
        }
    }
}

template <typename GFX>
static void subdial(GFX &g, int x, int y, int r)
{
    g.fillCircle(x, y, r, C_SUB);
    g.drawSmoothCircle(x, y, r, C_SUB_RING, C_DIAL);
}

template <typename GFX>
static void drawWeather(GFX &g)
{
    subdial(g, WX_CX, WX_CY, WX_R);
    wxIconColor(g, WX_CX, WX_CY - 8, wxValid ? iconForCode(wxCode) : WX_UNKNOWN,
                wxIsDay, C_SUB);

    char buf[8];
    if (wxValid) snprintf(buf, sizeof(buf), "%d", wxTempF);
    else         strcpy(buf, "--");
    g.setTextColor(C_TEXT, C_SUB);
    int tw   = g.textWidth(buf, 2);
    int left = WX_CX - (tw + 16) / 2;
    g.setTextDatum(ML_DATUM);
    g.drawString(buf, left, WX_CY + 13, 2);
    if (wxValid) {
        g.drawCircle(left + tw + 3, WX_CY + 13 - 5, 2, C_TEXT);
        g.drawString("F", left + tw + 7, WX_CY + 13, 2);
    }
}

template <typename GFX>
static void drawDate(GFX &g, const struct tm &t)
{
    int x = DATE_CX - DATE_S / 2, y = DATE_CY - DATE_S / 2;
    g.fillRoundRect(x, y, DATE_S, DATE_S, 8, C_SUB);
    g.drawRoundRect(x, y, DATE_S, DATE_S, 8, C_SUB_RING);

    char wd[8];
    strftime(wd, sizeof(wd), "%a", &t);
    for (char *p = wd; *p; p++) *p = toupper((unsigned char)*p);

    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_ACCENT, C_SUB);
    g.drawString(wd, DATE_CX, y + 3, 1);
    g.setTextColor(C_TEXT, C_SUB);
    g.drawNumber(t.tm_mday, DATE_CX, y + 12, 4);
}

template <typename GFX>
static void drawDayNight(GFX &g, const struct tm &t)
{
    subdial(g, SUN_CX, SUN_CY, SUN_R);
    bool night = isNightNow(t);
    wxIconColor(g, SUN_CX, SUN_CY - 6, WX_CLEAR, !night, C_SUB);

    // the next event: sunset while it is day, sunrise once it is night
    int next = night ? wxSunrise : wxSunset;
    char buf[8];
    if (next >= 0) snprintf(buf, sizeof(buf), "%02d:%02d", next / 60, next % 60);
    else           strcpy(buf, "--:--");
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_TEXT2, C_SUB);
    g.drawString(buf, SUN_CX, SUN_CY + 12, 1);
}

// A baton hand: black edge, white body, lume strip down the centre.
template <typename GFX>
static void batonHand(GFX &g, float ang, float back, float len, float w,
                      float lumeFrom, float lumeTo)
{
    float s = sinf(ang), c = cosf(ang);
    g.drawWideLine(CX - back * s, CY + back * c, CX + len * s, CY - len * c,
                   w + 2, C_HAND_EDGE, C_DIAL);
    g.drawWideLine(CX - back * s, CY + back * c, CX + len * s, CY - len * c,
                   w, C_HAND, C_HAND_EDGE);
    g.drawWideLine(CX + lumeFrom * s, CY - lumeFrom * c,
                   CX + lumeTo * s, CY - lumeTo * c, w * 0.4f, C_LUME, C_HAND);
}

template <typename GFX>
static void drawHands(GFX &g, float h, float m, float s)
{
    batonHand(g, h * 30.0f * DEG_TO_RAD, 14, 60, 9.0f, 14, 52);
    batonHand(g, m *  6.0f * DEG_TO_RAD, 16, 92, 7.0f, 18, 84);

    float as = s * 6.0f * DEG_TO_RAD;
    float sn = sinf(as), cs = cosf(as);
    g.drawWideLine(CX - 22 * sn, CY + 22 * cs, CX + 100 * sn, CY - 100 * cs,
                   1.8f, C_ACCENT, C_DIAL);
    g.fillSmoothCircle(lroundf(CX - 22 * sn), lroundf(CY + 22 * cs), 4, C_ACCENT, C_DIAL);

    g.fillSmoothCircle(CX, CY, 7, C_HAND, C_DIAL);
    g.fillSmoothCircle(CX, CY, 4, C_ACCENT, C_HAND);
    g.fillSmoothCircle(CX, CY, 2, statusCol, C_ACCENT);
}

template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    float s = t.tm_sec + subSec;
    float m = t.tm_min + s / 60.0f;
    float h = (t.tm_hour % 12) + m / 60.0f;

    drawDial(g);
    drawWeather(g);
    drawDate(g, t);
    drawDayNight(g, t);
    drawHands(g, h, m, s);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_modern

const FaceVTable FACE_MODERN = {
    "modern",
    face_modern::faceInit,
    face_modern::faceBackground,
    face_modern::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_modern::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_modern::faceRender,
};
