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
//     flux capacitor      plutonium gauge (WiFi signal)     TEMP
//     HOUR  MIN  SEC  YEAR  COND (weather code)
//     WEEK  DAY  MONTH (spelled)  SUNSET
//     SUNRISE  DAYLIGHT
//
//  The flux capacitor flickers on a three-count, like the prop: the three
//  arms light in sequence toward the centre. Nothing here is a bitmap; the
//  Y-shaped tube assembly is drawn from lines and discs.
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
#define C_GAUGE_BG  0xF7BE   // the gauge is a cream-faced analog meter
#define C_GAUGE_INK 0x2104
#define C_NEEDLE    0xF800
#define C_FLUX      0xFFE0   // flux tubes, lit
#define C_FLUX_OFF  0x4200
#define C_FLUX_BG   0x1082
#define C_YELLOW    0xFFE0

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
// Three tubes meeting at the centre in a Y, each a run of discs. They light
// in sequence, one arm at a time, which is the prop's signature flicker.
template <typename GFX>
static void fluxCapacitor(GFX &g, int cx, int cy, int r, int step)
{
    g.fillRect(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8, C_FLUX_BG);
    g.drawRect(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8, C_FRAME);

    // The prop is an upright Y: two arms up and out, one straight down. The
    // whole assembly sits a little high in its window, as the tubes are
    // longer above the junction than below.
    static const float ANG[3] = { 240.0f, 300.0f, 90.0f };
    int jy = cy + 4;                          // the junction, below centre
    for (int a = 0; a < 3; a++) {
        float rad = ANG[a] * DEG_TO_RAD;
        float sx = cosf(rad), sy = sinf(rad);
        bool  lit = (a == step);
        // the tube itself, dark, then the discs stepping along it
        int ex = cx + (int)(sx * r), ey = jy + (int)(sy * (a == 2 ? r - 6 : r));
        g.drawLine(cx, jy, ex, ey, C_FLUX_OFF);
        for (int i = 1; i <= 3; i++) {
            int px = cx + (int)(sx * r * i / 3.6f);
            int py = jy + (int)(sy * (a == 2 ? r - 6 : r) * i / 3.6f);
            g.fillCircle(px, py, 2, lit ? C_FLUX : C_FLUX_OFF);
        }
    }
    g.fillCircle(cx, jy, 3, C_FLUX);
}

// ---- plutonium gauge -------------------------------------------------------
// A cream-faced analog meter: arc scale, hatch marks, a red needle, and the
// chamber label under it. Driven by WiFi signal, which is the only thing on
// this clock that behaves like a level.
template <typename GFX>
static void gauge(GFX &g, int x, int y, int w, int h, int pct)
{
    g.fillRect(x, y, w, h, C_GAUGE_BG);
    g.drawRect(x, y, w, h, C_FRAME);

    int cx = x + w / 2, cy = y + h - 6;
    int r  = h - 18;                       // fits inside the meter face
    // the scale arc, from 210 to 330 degrees
    for (int a = 210; a <= 330; a += 4) {
        float rad = a * DEG_TO_RAD;
        g.drawPixel(cx + (int)(cosf(rad) * r), cy + (int)(sinf(rad) * r), C_GAUGE_INK);
    }
    for (int a = 210; a <= 330; a += 30) {
        float rad = a * DEG_TO_RAD;
        g.drawLine(cx + (int)(cosf(rad) * (r - 4)), cy + (int)(sinf(rad) * (r - 4)),
                   cx + (int)(cosf(rad) * r),       cy + (int)(sinf(rad) * r), C_GAUGE_INK);
    }
    // red danger band at the top of the scale
    for (int a = 310; a <= 330; a += 2) {
        float rad = a * DEG_TO_RAD;
        g.drawLine(cx + (int)(cosf(rad) * (r - 3)), cy + (int)(sinf(rad) * (r - 3)),
                   cx + (int)(cosf(rad) * r),       cy + (int)(sinf(rad) * r), C_RED);
    }
    // A fine needle from the pivot, stopping short of the scale. Plain lines
    // rather than drawWideLine: an anti-aliased line pads a pixel either
    // side, so anything under 3 px wide comes out the same weight, and a
    // meter needle wants to be hairline.
    float na = (210 + pct * 120 / 100) * DEG_TO_RAD;
    int nx = cx + (int)(cosf(na) * (r - 5)), ny = cy + (int)(sinf(na) * (r - 5));
    g.drawLine(cx, cy, nx, ny, C_NEEDLE);
    g.drawLine(cx + 1, cy, nx, ny, C_NEEDLE);      // a touch of body at the pivot
    g.fillCircle(cx, cy, 2, C_GAUGE_INK);

    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_GAUGE_INK, C_GAUGE_BG);
    g.drawString("PLUTONIUM", cx, y + 3, 1);
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

// A weather code condensed to three letters for the COND readout.
static const char *condWord(int code)
{
    switch (iconForCode(code)) {
        case WX_CLEAR:  return "CLR";
        case WX_PARTLY: return "PCL";
        case WX_CLOUD:  return "OVC";
        case WX_FOG:    return "FOG";
        case WX_RAIN:   return "RAI";
        case WX_SNOW:   return "SNO";
        case WX_STORM:  return "STM";
        default:        return "---";
    }
}

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
    int step = ((int)((t.tm_sec + subSec) * 3.0f)) % 3;
    fluxCapacitor(g, 68, 58, 22, step);

    int rssi = WiFi.RSSI();
    int sig  = (WiFi.status() == WL_CONNECTED) ? 2 * (rssi + 100) : 0;
    if (sig < 0)   sig = 0;
    if (sig > 100) sig = 100;
    gauge(g, 118, 32, 74, 46, sig);
    trefoil(g, 108, 88, 4);

    // ---- TEMP, at the right, as the original has it -----------------------
    tab(g, "TEMP", 188, 84, 40);
    well(g, 168, 96, 40, 22);
    if (wxValid) snprintf(buf, sizeof buf, "%2d", wxTempF);
    else         strcpy(buf, "--");
    digits(g, buf, 174, 99, 13, 16, 3, 4, C_RED, C_RED_OFF);

    // ---- destination row: HOUR MIN SEC YEAR COND --------------------------
    // labels
    tab(g, "HOUR", 40,  96, 34);
    tab(g, "MIN",  78,  96, 34);
    tab(g, "SEC",  116, 96, 34);
    tab(g, "YEAR", 156, 96, 40);
    // (TEMP's tab is drawn above, at the right)

    well(g, 23, 108, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_hour);
    digits(g, buf, 26, 111, 13, 18, 3, 3, C_RED, C_RED_OFF);

    well(g, 61, 108, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_min);
    digits(g, buf, 64, 111, 13, 18, 3, 3, C_RED, C_RED_OFF);

    well(g, 99, 108, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_sec);
    digits(g, buf, 102, 111, 13, 18, 3, 3, C_RED, C_RED_OFF);

    well(g, 137, 108, 50, 24);
    snprintf(buf, sizeof buf, "%04d", t.tm_year + 1900);
    digits(g, buf, 140, 111, 10, 18, 3, 2, C_RED, C_RED_OFF);

    // ---- present row: WEEK DAY MONTH (spelled) SUNSET ---------------------
    tab(g, "WEEK",  44,  138, 40);
    tab(g, "DAY",   86,  138, 34);
    tab(g, "MONTH", 134, 138, 52);
    tab(g, "SUNSET", 190, 138, 44);

    well(g, 23, 150, 42, 24);
    letters(g, WDAYS[t.tm_wday % 7], 26, 153, 11, 18, 3, 2, C_GRN, C_GRN_OFF);

    well(g, 69, 150, 34, 24);
    snprintf(buf, sizeof buf, "%02d", t.tm_mday);
    digits(g, buf, 72, 153, 13, 18, 3, 3, C_GRN, C_GRN_OFF);

    well(g, 107, 150, 42, 24);
    letters(g, MONTHS[t.tm_mon % 12], 110, 153, 11, 18, 3, 2, C_GRN, C_GRN_OFF);

    well(g, 153, 150, 52, 24);
    if (wxSunset >= 0) snprintf(buf, sizeof buf, "%02d%02d", wxSunset / 60, wxSunset % 60);
    else               strcpy(buf, "----");
    digits(g, buf, 156, 153, 11, 18, 3, 2, C_GRN, C_GRN_OFF);

    // ---- last departed row: SUNRISE DAYLIGHT COND -------------------------
    tab(g, "SUNRISE",  52, 180, 56);
    tab(g, "DAYLIGHT", 118, 180, 60);
    tab(g, "COND",     186, 180, 44);

    well(g, 23, 192, 58, 22);
    if (wxSunrise >= 0) snprintf(buf, sizeof buf, "%02d%02d", wxSunrise / 60, wxSunrise % 60);
    else                strcpy(buf, "----");
    digits(g, buf, 26, 195, 12, 16, 3, 2, C_AMB, C_AMB_OFF);

    well(g, 89, 192, 58, 22);
    if (wxSunrise >= 0 && wxSunset > wxSunrise) {
        int d = wxSunset - wxSunrise;
        snprintf(buf, sizeof buf, "%02d%02d", d / 60, d % 60);
    } else strcpy(buf, "----");
    digits(g, buf, 92, 195, 12, 16, 3, 2, C_AMB, C_AMB_OFF);

    well(g, 155, 192, 50, 22);
    letters(g, wxValid ? condWord(wxCode) : "---", 158, 195, 14, 16, 3, 2,
            C_AMB, C_AMB_OFF);

    // ---- the little indicators the prop carries ---------------------------
    g.setTextDatum(TL_DATUM);
    g.setTextColor(C_DIM, C_CASE);
    g.drawString("BT", 24, 84, 1);
    g.setTextColor(statusCol, C_CASE);
    g.fillCircle(38, 87, 2, statusCol);
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
    if (max > 0) out[n++] = { 68 - 30, 58 - 30, 60, 60 };
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
