// ---------------------------------------------------------------------------
//  Face: "delorean"  -  time circuits, flux capacitor, plutonium gauge
//
//  The DeLorean's dashboard time circuits: rows of seven-segment readouts on
//  black backing panels, each under a small white label on a red tab, in the
//  three colours the prop used - red for the destination row, green for the
//  present, amber for last-departed. Above them the flux capacitor flickers
//  in its window and the plutonium chamber gauge swings its red needle.
//
//  The original's health readouts become what this clock knows. Per the
//  brief, the month is spelled out rather than numbered, and the steps
//  readout gives way to it:
//
//     flux capacitor            plutonium gauge (WiFi signal)
//     HOUR  MIN  SEC  YEAR
//     WEEK  DAY  MONTH (spelled)  DAYLIGHT
//     SUNRISE/SUNSET (alternating)  TEMP  DAY NO
//
//  The flux capacitor charges: all three arms are live and pulses of light
//  run from the junction out to the terminals. Nothing here is a bitmap;
//  the whole assembly is drawn from lines and discs.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <WiFi.h>

namespace face_delorean {

// ---- palette ---------------------------------------------------------------
#define C_BG        0x0000
#define C_CASE      0x18E3   // the brushed panel between the readouts
#define C_PANEL     0x0000   // black backing behind each row of digits
#define C_FRAME     0x39E7   // panel edging
#define C_RED       0xF800   // destination row + label tabs
#define C_RED_OFF   0x2000   // its unlit segments
#define C_GRN       0x07E0   // present time row
#define C_GRN_OFF   0x0200
#define C_AMB       0xFD20   // last departed row
#define C_AMB_OFF   0x2900
#define C_LABEL     0xFFFF   // white label text on the red tabs
#define C_TEXT      0xC618
#define C_DIM       0x7BEF
// the gauge: a cream-faced moving-coil meter in a black bezel
#define C_GAUGE_BG    0xF7BE   // the printed face
#define C_GAUGE_INK   0x1082   // its printing
#define C_GAUGE_EDGE  0x8410   // the inset line around the face
#define C_GAUGE_BEZEL 0x0000   // the surround
#define C_NEEDLE      0x0000   // a black hairline, as on the real instrument

// the flux capacitor: glass tubes over a black box
#define C_FLUX_BOX    0x0000   // the housing interior
#define C_FLUX_BEZEL  0x8410   // its bright edge
#define C_FLUX_SHADOW 0x2104   // the inner shadow just inside that
#define C_TUBE_WALL   0x39E7   // the thick body of each tube
#define C_TUBE_CORE   0x6B4D   // glass down the middle, catching light
#define C_TERMINAL    0x4208   // blocks where the tubes meet the housing
#define C_FLUX        0xFFE0   // a lit bulb
#define C_FLUX_HOT    0xFFFF   // its white-hot centre
#define C_FLUX_HALO   0xFC00   // the warm spill around it
#define C_FLUX_OFF    0x4A29   // an unlit bulb
#define C_FLUX_DIM    0x8C51   // its filament, still just visible
#define C_YELLOW      0xFFE0

uint16_t faceBackground() { return C_BG; }
bool     faceSmooth()     { return true; }   // the capacitor flickers
void     faceInit()       { }

static const uint8_t SEG_MAP[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// A 14-segment starburst alphabet, enough for the month names and the
// weekday. The seven-segment cells cannot make an M or a W at all, which is
// why the spelled-out month needs these.
static uint16_t mask14(char c)
{
    switch (c) {
        case 'A': return 0x00F7; case 'B': return 0x128F; case 'C': return 0x0039;
        case 'D': return 0x120F; case 'E': return 0x00F9; case 'F': return 0x00F1;
        case 'G': return 0x00BD; case 'H': return 0x00F6; case 'I': return 0x1209;
        case 'J': return 0x001E; case 'K': return 0x2470; case 'L': return 0x0038;
        case 'M': return 0x0536; case 'N': return 0x2136; case 'O': return 0x003F;
        case 'P': return 0x00F3; case 'Q': return 0x203F; case 'R': return 0x20F3;
        case 'S': return 0x00ED; case 'T': return 0x1201; case 'U': return 0x003E;
        case 'V': return 0x0C30; case 'W': return 0x2836; case 'X': return 0x2D00;
        case 'Y': return 0x1500; case 'Z': return 0x0C09;
        default:  return 0x0000;
    }
}

// ---- cells -----------------------------------------------------------------
template <typename GFX>
static void seg7(GFX &g, int x, int y, int w, int h, int t, int d,
                 uint16_t on, uint16_t off)
{
    uint8_t m    = (d >= 0 && d <= 9) ? SEG_MAP[d] : 0;
    int     half = (h - t) / 2;
    int     vh   = (h - 3 * t) / 2;
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
        g.fillRect(s[i].x, s[i].y, s[i].w, s[i].h, (m & s[i].bit) ? on : off);
}

// 14 segments: the 7 above, the two middle bars split, plus four diagonals
// and two centre uprights.
template <typename GFX>
static void seg14(GFX &g, int x, int y, int w, int h, int t, uint16_t m,
                  uint16_t on, uint16_t off)
{
    int half = (h - t) / 2;
    int vh   = (h - 3 * t) / 2;
    if (vh < 1) vh = 1;
    int mx   = x + (w - t) / 2;
    int hw   = (w - 2 * t) / 2;
    struct S { uint16_t bit; int x, y, w, h; } s[] = {
        {0x0001, x + t,      y,            w - 2 * t, t },   // A
        {0x0002, x + w - t,  y + t,        t,         vh},   // B
        {0x0004, x + w - t,  y + half + t, t,         vh},   // C
        {0x0008, x + t,      y + h - t,    w - 2 * t, t },   // D
        {0x0010, x,          y + half + t, t,         vh},   // E
        {0x0020, x,          y + t,        t,         vh},   // F
        {0x0040, x + t,      y + half,     hw,        t },   // G1
        {0x0080, mx,         y + half,     hw,        t },   // G2
        {0x0200, mx,         y + t,        t,         vh},   // I (top centre)
        {0x1000, mx,         y + half + t, t,         vh},   // L (bottom centre)
    };
    for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++)
        g.fillRect(s[i].x, s[i].y, s[i].w, s[i].h, (m & s[i].bit) ? on : off);
    // diagonals: H (top-left), K (top-right), M (bottom-left), N (bottom-right)
    struct D { uint16_t bit; int x0, y0, x1, y1; } d[] = {
        {0x0100, x + t,     y + t,        mx,        y + half - 1},
        {0x0400, x + w - t, y + t,        mx + t,    y + half - 1},
        {0x0800, x + t,     y + h - t,    mx,        y + half + t},
        {0x2000, x + w - t, y + h - t,    mx + t,    y + half + t},
    };
    for (unsigned i = 0; i < 4; i++)
        if (m & d[i].bit) {
            g.drawLine(d[i].x0, d[i].y0, d[i].x1, d[i].y1, on);
            g.drawLine(d[i].x0 + 1, d[i].y0, d[i].x1 + 1, d[i].y1, on);
        }
}

// A row of seven-segment digits from a string; '-' and ' ' blank the cell.
template <typename GFX>
static void digits(GFX &g, const char *s, int x, int y, int w, int h, int t,
                   int gap, uint16_t on, uint16_t off)
{
    for (; *s; s++, x += w + gap)
        seg7(g, x, y, w, h, t, (*s >= '0' && *s <= '9') ? *s - '0' : -1, on, off);
}

template <typename GFX>
static void letters(GFX &g, const char *s, int x, int y, int w, int h, int t,
                    int gap, uint16_t on, uint16_t off)
{
    for (; *s; s++, x += w + gap)
        seg14(g, x, y, w, h, t, mask14(*s), on, off);
}

// The white-on-red label tab above each readout.
template <typename GFX>
static void tab(GFX &g, const char *s, int cx, int y, int w)
{
    g.fillRect(cx - w / 2, y, w, 11, C_RED);
    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_LABEL, C_RED);
    g.drawString(s, cx, y + 1, 1);
}

// Black backing panel with a thin edge, the way each readout is recessed.
template <typename GFX>
static void well(GFX &g, int x, int y, int w, int h)
{
    g.fillRect(x, y, w, h, C_PANEL);
    g.drawRect(x, y, w, h, C_FRAME);
}

// ---- flux capacitor --------------------------------------------------------
// The prop is three thick glass tubes in a Y inside a black box: two rising
// to the upper corners, one dropping to the bottom centre, meeting at a
// junction below the middle. Each tube is a wide dark channel with a lighter
// core (that reads as glass), a bulb at its outer end where it enters a
// terminal block, and four smaller bulbs stepping down it toward the
// junction. All three arms are live at once and a pulse of light travels
// from the junction outward along every arm, which is the prop charging.
//
// Drawn in that order - box, tube walls, tube cores, terminals, bulbs - so
// each layer sits over the one behind it, the way the real assembly stacks.
template <typename GFX>
static void fluxCapacitor(GFX &g, int x, int y, int w, int h, float phase)
{
    // the housing: black box, thin bright bezel, a darker inner shadow
    g.fillRect(x, y, w, h, C_FLUX_BOX);
    g.drawRect(x, y, w, h, C_FLUX_BEZEL);
    g.drawRect(x + 1, y + 1, w - 2, h - 2, C_FLUX_SHADOW);

    const int cx = x + w / 2;
    const int jy = y + h * 42 / 100;          // the junction sits above centre
    // The prop's Y stands upright: two arms rise to the upper corners and
    // one drops from the junction to the bottom centre.
    const int ax[3] = { x + 10,     x + w - 10, cx };
    const int ay[3] = { y + 11,     y + 11,     y + h - 9 };

    // tube walls first, thick and dark
    for (int a = 0; a < 3; a++) {
        for (int o = -3; o <= 3; o++)
            g.drawLine(cx + o, jy, ax[a] + o, ay[a], C_TUBE_WALL);
    }
    // then the glass core down the middle of each
    for (int a = 0; a < 3; a++) {
        for (int o = -1; o <= 1; o++)
            g.drawLine(cx + o, jy, ax[a] + o, ay[a], C_TUBE_CORE);
    }

    // terminal blocks where the tubes meet the housing
    for (int a = 0; a < 3; a++) {
        g.fillRect(ax[a] - 5, ay[a] - 4, 10, 8, C_TERMINAL);
        g.drawRect(ax[a] - 5, ay[a] - 4, 10, 8, C_FLUX_BEZEL);
    }

    // The bulbs: four down each arm, and all three arms fire together. A
    // pulse travels from the junction out to the terminals, so each bulb
    // brightens as the pulse reaches it and fades behind it. That is the
    // charging flow, rather than one arm at a time.
    //
    // `phase` runs 0..1 over one pulse. A bulb at position p along its arm
    // is brightest when the pulse is at p, and its brightness falls off with
    // the distance between them - so the light appears to move outward.
    for (int a = 0; a < 3; a++) {
        for (int i = 0; i < 4; i++) {
            float p  = 0.22f + i * 0.24f;         // where this bulb sits, 0..1
            int   bx = cx + (int)((ax[a] - cx) * p);
            int   by = jy + (int)((ay[a] - jy) * p);

            float d = phase - p;                   // how far the pulse is past it
            if (d < 0) d += 1.0f;                  // the pulse wraps around
            // a short bright head with a tail behind it
            int level;
            if      (d < 0.18f) level = 3;         // the pulse is on this bulb
            else if (d < 0.36f) level = 2;         // just behind it
            else if (d < 0.58f) level = 1;         // fading
            else                level = 0;         // idle

            switch (level) {
                case 3:
                    g.fillCircle(bx, by, 5, C_FLUX_HALO);
                    g.fillCircle(bx, by, 3, C_FLUX);
                    g.fillCircle(bx, by, 2, C_FLUX_HOT);
                    break;
                case 2:
                    g.fillCircle(bx, by, 4, C_FLUX_HALO);
                    g.fillCircle(bx, by, 3, C_FLUX);
                    g.fillCircle(bx, by, 1, C_FLUX_HOT);
                    break;
                case 1:
                    g.fillCircle(bx, by, 3, C_FLUX);
                    g.fillCircle(bx, by, 1, C_FLUX_HOT);
                    break;
                default:
                    g.fillCircle(bx, by, 3, C_FLUX_OFF);
                    g.fillCircle(bx, by, 1, C_FLUX_DIM);
                    break;
            }
        }
    }

    // The junction, where every pulse is born: brightest as one leaves.
    int jr = (phase < 0.18f) ? 6 : 5;
    g.fillCircle(cx, jy, jr + 1, C_FLUX_HALO);
    g.fillCircle(cx, jy, jr, C_FLUX);
    g.fillCircle(cx, jy, jr - 3, C_FLUX_HOT);
}

// ---- plutonium gauge -------------------------------------------------------
// A moving-coil panel meter: cream face in a black bezel, a printed arc with
// long ticks at the divisions and short ones between, a red band over the
// top third, PLUTONIUM printed under the arc, and a hairline black needle
// pivoting from a hub at the bottom with a short counterweight tail. Driven
// by WiFi signal, the only thing on this clock that behaves like a level.
//
// The needle is drawn as plain one-pixel lines: an anti-aliased line pads a
// pixel either side, so anything thinner than three pixels comes out the
// same weight, and a meter needle has to be finer than that to look real.
template <typename GFX>
static void gauge(GFX &g, int x, int y, int w, int h, int pct)
{
    // bezel, then the meter face inset within it
    g.fillRect(x, y, w, h, C_GAUGE_BEZEL);
    g.fillRect(x + 3, y + 3, w - 6, h - 6, C_GAUGE_BG);
    g.drawRect(x + 3, y + 3, w - 6, h - 6, C_GAUGE_EDGE);

    const int cx = x + w / 2;
    const int cy = y + h - 9;               // pivot near the bottom edge
    const int r  = h - 22;                  // scale radius
    const int A0 = 208, A1 = 332;           // the swept arc

    // the printed arc itself, drawn densely enough to be a continuous line
    for (int a = A0; a <= A1; a++) {
        float rad = a * DEG_TO_RAD;
        g.drawPixel(cx + (int)(cosf(rad) * r), cy + (int)(sinf(rad) * r), C_GAUGE_INK);
        g.drawPixel(cx + (int)(cosf(rad) * (r - 1)), cy + (int)(sinf(rad) * (r - 1)),
                    C_GAUGE_INK);
    }

    // graduations: long every quarter of the sweep, short between
    for (int i = 0; i <= 12; i++) {
        int   a   = A0 + (A1 - A0) * i / 12;
        float rad = a * DEG_TO_RAD;
        int   len = (i % 3 == 0) ? 6 : 3;
        g.drawLine(cx + (int)(cosf(rad) * (r - len)), cy + (int)(sinf(rad) * (r - len)),
                   cx + (int)(cosf(rad) * (r - 1)),   cy + (int)(sinf(rad) * (r - 1)),
                   C_GAUGE_INK);
    }

    // the red band over the top of the scale, as a thick arc outside the line
    for (int a = A0 + (A1 - A0) * 8 / 12; a <= A1; a++) {
        float rad = a * DEG_TO_RAD;
        for (int o = 1; o <= 3; o++)
            g.drawPixel(cx + (int)(cosf(rad) * (r + o)), cy + (int)(sinf(rad) * (r + o)),
                        C_RED);
    }

    // the label, printed on the face under the arc
    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_GAUGE_INK, C_GAUGE_BG);
    g.drawString("PLUTONIUM", cx, cy - 11, 1);

    // the needle: a hairline out to just short of the arc, plus a stub of
    // counterweight on the far side of the pivot
    float na = (A0 + (A1 - A0) * pct / 100) * DEG_TO_RAD;
    float ns = sinf(na), nc = cosf(na);
    g.drawLine(cx, cy, cx + (int)(nc * (r - 4)), cy + (int)(ns * (r - 4)), C_NEEDLE);
    g.drawLine(cx - (int)(nc * 5), cy - (int)(ns * 5), cx, cy, C_NEEDLE);

    // the hub, a small black boss with a highlight
    g.fillCircle(cx, cy, 3, C_GAUGE_INK);
    g.drawPixel(cx - 1, cy - 1, C_GAUGE_BG);
}

// Radiation trefoil, the little yellow warning by the gauge.
template <typename GFX>
static void trefoil(GFX &g, int cx, int cy, int r)
{
    g.fillCircle(cx, cy, r, C_YELLOW);
    for (int i = 0; i < 3; i++) {
        float a = (i * 120 + 90) * DEG_TO_RAD;
        g.fillTriangle(cx, cy,
                       cx + cosf(a - 0.5f) * r * 2, cy + sinf(a - 0.5f) * r * 2,
                       cx + cosf(a + 0.5f) * r * 2, cy + sinf(a + 0.5f) * r * 2,
                       C_YELLOW);
    }
    g.fillCircle(cx, cy, r / 2, C_BG);
}

static const char *MONTHS[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};
static const char *WDAYS[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// ---- the face ---------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    char buf[16];

    g.fillScreen(C_BG);
    // the dark brushed housing the readouts are set into
    g.fillRoundRect(14, 22, 212, 196, 8, C_CASE);
    g.drawRoundRect(14, 22, 212, 196, 8, C_FRAME);

    // ---- flux capacitor and gauge across the top --------------------------
    float ticks = (t.tm_sec + subSec) / 0.7f;      // one pulse every 0.7 s
    fluxCapacitor(g, 30, 30, 62, 62, ticks - (int)ticks);

    int rssi = WiFi.RSSI();
    int sig  = (WiFi.status() == WL_CONNECTED) ? 2 * (rssi + 100) : 0;
    if (sig < 0)   sig = 0;
    if (sig > 100) sig = 100;
    gauge(g, 104, 32, 92, 58, sig);
    trefoil(g, 207, 66, 4);

    // ---- destination row: HOUR MIN SEC YEAR -------------------------------
    tab(g, "HOUR", 40,  96, 34);
    tab(g, "MIN",  78,  96, 34);
    tab(g, "SEC",  116, 96, 34);
    tab(g, "YEAR", 162, 96, 52);

    well(g, 23, 108, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_hour);
    digits(g, buf, 26, 111, 13, 18, 3, 3, C_RED, C_RED_OFF);

    well(g, 61, 108, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_min);
    digits(g, buf, 64, 111, 13, 18, 3, 3, C_RED, C_RED_OFF);

    well(g, 99, 108, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_sec);
    digits(g, buf, 102, 111, 13, 18, 3, 3, C_RED, C_RED_OFF);

    well(g, 137, 108, 56, 24);
    snprintf(buf, sizeof buf, "%04d", t.tm_year + 1900);
    digits(g, buf, 141, 111, 11, 18, 3, 2, C_RED, C_RED_OFF);

    // ---- present row: WEEK DAY MONTH (spelled) SUNSET ---------------------
    tab(g, "WEEK",  44,  138, 40);
    tab(g, "DAY",   86,  138, 34);
    tab(g, "MONTH", 134, 138, 52);
    tab(g, "DAYLIGHT", 190, 138, 56);

    well(g, 23, 150, 42, 24);
    letters(g, WDAYS[t.tm_wday % 7], 26, 153, 11, 18, 3, 2, C_GRN, C_GRN_OFF);

    well(g, 69, 150, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_mday);
    digits(g, buf, 72, 153, 13, 18, 3, 3, C_GRN, C_GRN_OFF);

    well(g, 107, 150, 42, 24);
    letters(g, MONTHS[t.tm_mon % 12], 110, 153, 11, 18, 3, 2, C_GRN, C_GRN_OFF);

    well(g, 153, 150, 52, 24);
    if (wxSunrise >= 0 && wxSunset > wxSunrise) {
        int d = wxSunset - wxSunrise;
        snprintf(buf, sizeof buf, "%02d%02d", d / 60, d % 60);
    } else strcpy(buf, "----");
    digits(g, buf, 156, 153, 11, 18, 3, 2, C_GRN, C_GRN_OFF);

    // ---- last departed row: sun times | TEMP | DAYLIGHT --------------------
    // One box carries both sun times, swapping every five seconds, which
    // keeps a readout of its own for the temperature.
    bool showRise = ((t.tm_sec / 5) & 1) == 0;
    tab(g, showRise ? "SUNRISE" : "SUNSET", 52, 180, 56);
    well(g, 23, 192, 58, 22);
    int sunVal = showRise ? wxSunrise : wxSunset;
    if (sunVal >= 0) snprintf(buf, sizeof buf, "%02d%02d", sunVal / 60, sunVal % 60);
    else             strcpy(buf, "----");
    digits(g, buf, 26, 195, 12, 16, 3, 2, C_AMB, C_AMB_OFF);

    tab(g, "TEMP", 118, 180, 56);
    well(g, 89, 192, 58, 22);
    if (wxValid) snprintf(buf, sizeof buf, "%3d", wxTempF);
    else         strcpy(buf, " --");
    digits(g, buf, 93, 195, 12, 16, 3, 3, C_AMB, C_AMB_OFF);

    // the year's remaining daylight is up in the green row now, so this
    // last box carries the date's day-of-year, which the prop had as a
    // running counter
    tab(g, "DAY NO", 184, 180, 60);
    well(g, 155, 192, 58, 22);
    snprintf(buf, sizeof buf, "%03d", t.tm_yday + 1);
    digits(g, buf, 163, 195, 12, 16, 3, 3, C_AMB, C_AMB_OFF);

    // ---- the little indicators the prop carries ---------------------------
    // WiFi/NTP state: a lamp on the housing beside the gauge. There is no
    // room for a caption up here, and the prop has unlabelled lamps anyway.
    g.fillCircle(207, 44, 4, 0x2104);
    g.fillCircle(207, 44, 3, statusCol);
    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_TEXT, C_BG);
    g.drawString("OUTATIME", 120, 8, 1);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

// Only the flux capacitor animates between seconds; the readouts change on
// the second. Lets the AMOLED board redraw just that window at 8 Hz.
static int faceDirty(const struct tm &t, float, const struct tm &pt, float,
                     DirtyRect *out, int max)
{
    int n = 0;
    if (max > 0) out[n++] = { 30, 30, 62, 62 };          // the capacitor box
    if (t.tm_sec != pt.tm_sec && n < max) out[n++] = { 20, 92, 200, 126 };
    return n;
}

} // namespace face_delorean

const FaceVTable FACE_DELOREAN = {
    "delorean",
    face_delorean::faceInit,
    face_delorean::faceBackground,
    face_delorean::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_delorean::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_delorean::faceRender,
    face_delorean::faceDirty,
};
