// ---------------------------------------------------------------------------
//  Face: "outrun"  -  synthwave sun over a perspective grid
//
//  The 1980s album-cover horizon: a big disc sitting on a vanishing-point
//  grid, sliced by horizontal bands, under a wide seven-segment clock. A
//  label strip carries the weekday and date, and two panels along the bottom
//  hold the temperature and the WiFi signal.
//
//  The palette moves with the sun rather than with the clock, because the
//  whole point of the picture is the time of day it depicts. Five of them,
//  crossfaded so the change is never a jump:
//
//     midnight  near-black, dim cyan - almost an always-on display
//     night     deep indigo, cyan digits, a magenta sun
//     dawn      violet and lilac
//     day       blue sky, amber sun, gold grid
//     dusk      crimson and hot orange
//
//  Sunrise and sunset come from the weather feed where it has answered, so
//  dawn and dusk land at the real ones. Failing that it falls back to fixed
//  hours, which is wrong by up to an hour or so but never looks broken.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <WiFi.h>

namespace face_outrun {

#define CX      120
#define HORIZON 138          // where the grid meets the sky
#define SUN_CY  128          // the disc sits astride the horizon
#define SUN_R    30

// ---- palettes --------------------------------------------------------------
// Sampled off the reference art. `skyTop` fades to `skyBot` down to the
// horizon; `grid` is the perspective floor; `ink` is the digits; `sun` and
// `sunLow` are the top and bottom of the disc, which is itself a gradient.
struct Pal {
    uint16_t skyTop, skyBot, grid, gridDim, ink, sun, sunLow, panel, label;
};

static const Pal P_MIDNIGHT = {
    0x0021, 0x0842, 0x3186, 0x18C3, 0x3DF9, 0x79ED, 0x8A87, 0x1082, 0x4A69
};
static const Pal P_NIGHT = {
    0x0823, 0x1866, 0x8811, 0x4008, 0x479E, 0xF9F1, 0xFBC7, 0x1084, 0x738E
};
static const Pal P_DAWN = {
    0x1866, 0x494D, 0xA25C, 0x5AAB, 0x7F5E, 0xBC1F, 0xFD4F, 0x2124, 0x9CDF
};
static const Pal P_DAY = {
    0x10E9, 0x2A30, 0xB460, 0x5A08, 0x7EFF, 0xFDE7, 0xFCA5, 0x18E3, 0xAD75
};
static const Pal P_DUSK = {
    0x2822, 0x6884, 0xF9A0, 0x8140, 0xFC65, 0xF9ED, 0xFAC5, 0x2082, 0xFBAE
};

static Pal pal = P_NIGHT;

uint16_t faceBackground() { return pal.skyTop; }
// Nothing here moves between seconds - the colon blinks on the tick and the
// palette creeps over minutes - so this redraws once a second rather than
// flat out. On the banded display that is the difference between one frame a
// second and ten, for a picture that looks identical either way.
bool     faceSmooth()     { return false; }
void     faceInit()       { }

// Blend two 565 colours, t in 0..255.
static uint16_t mix(uint16_t a, uint16_t b, int t)
{
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + ((br - ar) * t >> 8);
    int g = ag + ((bg - ag) * t >> 8);
    int bl = ab + ((bb - ab) * t >> 8);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static Pal mixPal(const Pal &a, const Pal &b, int t)
{
    Pal p;
    p.skyTop  = mix(a.skyTop,  b.skyTop,  t);
    p.skyBot  = mix(a.skyBot,  b.skyBot,  t);
    p.grid    = mix(a.grid,    b.grid,    t);
    p.gridDim = mix(a.gridDim, b.gridDim, t);
    p.ink     = mix(a.ink,     b.ink,     t);
    p.sun     = mix(a.sun,     b.sun,     t);
    p.sunLow  = mix(a.sunLow,  b.sunLow,  t);
    p.panel   = mix(a.panel,   b.panel,   t);
    p.label   = mix(a.label,   b.label,   t);
    return p;
}

// Which palette this minute wants. Anchored on the real sunrise and sunset
// where the weather feed has given them, so dawn happens at dawn.
static void choosePalette(const struct tm &t)
{
    int now  = t.tm_hour * 60 + t.tm_min;
    int rise = (wxSunrise >= 0) ? wxSunrise : 6 * 60 + 30;
    int set  = (wxSunset  >= 0) ? wxSunset  : 19 * 60 + 30;

    // Dawn and dusk are the hour either side of the event; the rest of the
    // day and night hold their own palette, with midnight taking over in the
    // small hours when nobody is looking and the panel may as well be dark.
    const int BAND = 60;

    if (now >= rise - BAND && now < rise)                    // night -> dawn
        pal = mixPal(P_NIGHT, P_DAWN, (now - (rise - BAND)) * 255 / BAND);
    else if (now >= rise && now < rise + BAND)               // dawn -> day
        pal = mixPal(P_DAWN, P_DAY, (now - rise) * 255 / BAND);
    else if (now >= rise + BAND && now < set - BAND)         // full day
        pal = P_DAY;
    else if (now >= set - BAND && now < set)                 // day -> dusk
        pal = mixPal(P_DAY, P_DUSK, (now - (set - BAND)) * 255 / BAND);
    else if (now >= set && now < set + BAND)                 // dusk -> night
        pal = mixPal(P_DUSK, P_NIGHT, (now - set) * 255 / BAND);
    else if (now >= 1 * 60 && now < 4 * 60)                  // the small hours
        pal = P_MIDNIGHT;
    else if (now >= 0 && now < 1 * 60)                       // night -> midnight
        pal = mixPal(P_NIGHT, P_MIDNIGHT, now * 255 / 60);
    else if (now >= 4 * 60 && now < 5 * 60)                  // midnight -> night
        pal = mixPal(P_MIDNIGHT, P_NIGHT, (now - 4 * 60) * 255 / 60);
    else
        pal = P_NIGHT;
}

// ---- the picture -----------------------------------------------------------
// Sky: a vertical fade, drawn as rows. Cheap, and the only way to get the
// gradient the reference has.
template <typename GFX>
static void drawSky(GFX &g)
{
    for (int y = 0; y < HORIZON; y++) {
        int t = y * 255 / (HORIZON - 1);
        g.drawFastHLine(0, y, 240, mix(pal.skyTop, pal.skyBot, t));
    }
}

// The grid: lines converging on a vanishing point at the horizon, and
// horizontals that bunch up as they recede. Both are what sells the
// perspective, and neither is expensive.
template <typename GFX>
static void drawGrid(GFX &g)
{
    g.fillRect(0, HORIZON, 240, 240 - HORIZON, pal.skyBot);

    // verticals, fanning out from the vanishing point
    for (int i = -7; i <= 7; i++) {
        int xb = CX + i * 46;                 // where it crosses the bottom
        g.drawWideLine(CX, HORIZON, xb, 240, 1.4f, pal.gridDim, pal.skyBot);
    }
    // Horizontals, crowding towards the horizon: a receding plane puts its
    // lines closer together the further away they are, so the spacing has to
    // open up as it comes towards the viewer, not close down.
    for (int i = 1; i <= 7; i++) {
        float f = 1.0f - (1.0f - i / 7.0f) * (1.0f - i / 7.0f);
        int   y = HORIZON + (int)((240 - HORIZON) * f);
        if (y >= 240) break;
        g.drawWideLine(0, y, 240, y, 1.4f, pal.grid, pal.skyBot);
    }
    g.drawWideLine(0, HORIZON, 240, HORIZON, 2.0f, pal.grid, pal.skyBot);
}

// The sun: a disc with a vertical gradient, sliced by bands that widen
// towards the bottom, exactly as the album covers draw it.
template <typename GFX>
static void drawSun(GFX &g)
{
    for (int dy = -SUN_R; dy <= SUN_R; dy++) {
        int y = SUN_CY + dy;
        if (y < 0 || y >= HORIZON + 12) continue;

        // The slices start around the middle of the disc rather than near
        // its foot: below the horizon there is nothing left to slice, so
        // starting late meant the bands never appeared at all.
        if (dy > -10) {
            int band = dy + 10;
            if ((band / 4) % 2 == 1) continue;        // a gap
        }

        int half = (int)(sqrtf((float)(SUN_R * SUN_R - dy * dy)) + 0.5f);
        if (half <= 0) continue;
        uint16_t c = mix(pal.sun, pal.sunLow, (dy + SUN_R) * 255 / (2 * SUN_R));
        g.drawFastHLine(CX - half, y, 2 * half, c);
    }
}

// ---- readouts --------------------------------------------------------------
static const uint8_t SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// One seven-segment digit, outline style: the reference draws them as hollow
// strokes rather than solid bars, which is what makes them read as neon.
template <typename GFX>
static void digit(GFX &g, int x, int y, int w, int h, int d, uint16_t c)
{
    uint8_t m = (d >= 0 && d <= 9) ? SEG[d] : 0;
    const int t = 3;
    int half = (h - t) / 2;
    int vh   = (h - 3 * t) / 2;
    if (vh < 1) vh = 1;
    struct S { uint8_t bit; int x, y, w, h; } s[7] = {
        {0x01, x + t,     y,            w - 2 * t, t },
        {0x20, x,         y + t,        t,         vh},
        {0x02, x + w - t, y + t,        t,         vh},
        {0x40, x + t,     y + half,     w - 2 * t, t },
        {0x10, x,         y + half + t, t,         vh},
        {0x04, x + w - t, y + half + t, t,         vh},
        {0x08, x + t,     y + h - t,    w - 2 * t, t },
    };
    for (int i = 0; i < 7; i++)
        if (m & s[i].bit) g.fillRect(s[i].x, s[i].y, s[i].w, s[i].h, c);
}

// A bordered panel, the way the reference frames its complications.
template <typename GFX>
static void panel(GFX &g, int x, int y, int w, int h)
{
    g.fillRoundRect(x, y, w, h, 4, pal.panel);
    g.drawRoundRect(x, y, w, h, 4, pal.label);
}

// ---- the face --------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;
    choosePalette(t);

    drawSky(g);
    drawSun(g);
    drawGrid(g);

    // OUTRUN, small and wide across the top
    g.setTextDatum(TC_DATUM);
    g.setTextColor(pal.label, pal.skyTop);
    g.drawString("O U T R U N", CX, 18, 1);

    // The date strip. Filled, with a border only along the top and bottom:
    // the reference has the weekday and date sitting between two rules, and
    // a full box round it turned into a pair of bright end caps that read as
    // part of the type.
    static const char *WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    g.fillRoundRect(CX - 56, 30, 112, 20, 4, pal.panel);
    g.drawFastHLine(CX - 52, 30,      104, pal.label);
    g.drawFastHLine(CX - 52, 30 + 19, 104, pal.label);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(pal.ink, pal.panel);
    char buf[16];
    snprintf(buf, sizeof buf, "%s  %d", WD[t.tm_wday % 7], t.tm_mday);
    g.drawString(buf, CX, 40, 2);

    // the time, big, 24 hour like every digital face here
    const int DW = 26, DH = 42, GAP = 5;
    int x = CX - (4 * DW + 3 * GAP + 10) / 2;
    digit(g, x,                    58, DW, DH, t.tm_hour / 10, pal.ink);
    digit(g, x + DW + GAP,         58, DW, DH, t.tm_hour % 10, pal.ink);
    // The colon blinks on the second, the way a digital clock does - it is
    // the only thing on this face that moves, and a dead colon on a static
    // dial reads as a stopped clock.
    if (t.tm_sec & 1) {
        g.fillRect(x + 2 * (DW + GAP) + 2, 58 + DH / 3,     4, 4, pal.ink);
        g.fillRect(x + 2 * (DW + GAP) + 2, 58 + 2 * DH / 3, 4, 4, pal.ink);
    }
    digit(g, x + 2 * (DW + GAP) + 10, 58, DW, DH, t.tm_min / 10, pal.ink);
    digit(g, x + 3 * (DW + GAP) + 10, 58, DW, DH, t.tm_min % 10, pal.ink);

    // Temperature and signal. Placed from the circle rather than by eye: at
    // this row the glass allows about 142 px, which is two 66s and a gap.
    panel(g, 48, 176, 66, 28);
    panel(g, 124, 176, 66, 28);

    g.setTextDatum(MC_DATUM);
    g.setTextColor(pal.ink, pal.panel);
    if (wxValid) snprintf(buf, sizeof buf, "%d%c", wxTempF, 0xB0);
    else         strcpy(buf, "--");
    // Font 2, not 4: a 26 px glyph in a 28 px panel touches both rules.
    g.drawString(buf, 81, 186, 2);
    g.setTextColor(pal.label, pal.panel);
    g.drawString("WEATHER", 81, 197, 1);

    // The sky in a word, rather than a signal strength: this face is a
    // picture of the weather, so the panel beside the temperature may as
    // well say what it is.
    g.setTextColor(pal.ink, pal.panel);
    g.drawString(wxValid ? wxName(iconForCode(wxCode)) : "--", 157, 186, 2);
    g.setTextColor(pal.label, pal.panel);
    g.drawString("CONDITIONS", 157, 197, 1);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_outrun

const FaceVTable FACE_OUTRUN = {
    "outrun",
    face_outrun::faceInit,
    face_outrun::faceBackground,
    face_outrun::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_outrun::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_outrun::faceRender,
};
