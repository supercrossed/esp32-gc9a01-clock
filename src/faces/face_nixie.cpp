// ---------------------------------------------------------------------------
//  Face: "nixie"  -  four IN-12 tubes on a dark chassis
//
//  A nixie tube is not a segment display. Each digit is a separate wire
//  cathode, all ten stacked one behind another inside the glass, and the one
//  that is lit glows orange while the other nine hang in front of and behind
//  it catching a little light. That stack is the whole character of the
//  thing: the numeral has depth, it is never quite flat on, and the dark
//  cathodes are visible around it.
//
//  So this draws the stack. Every tube shows its lit digit in the neon
//  orange, plus two of the unlit ones offset a pixel or two, dark and thin.
//  Around the lit numeral is a halo, because the gas discharge spills onto
//  the glass and that spill is most of what you see across a room.
//
//  The tubes sit in a chassis with the wire mesh anode in front of them, a
//  pair of colon neons between the pairs, and the date and weather printed
//  on the panel below. The neons flash on the second the way the real ones
//  are wired to.
//
//  Digits are drawn as strokes rather than segments - a nixie cathode is
//  bent wire, so the numerals have rounded corners and open ends, and a
//  seven-segment shape reads as an LED clock rather than a tube.
// ---------------------------------------------------------------------------
#include "../face.h"

namespace face_nixie {

#define CX      120
#define CY      120

// ---- palette ---------------------------------------------------------------
#define C_CASE     0x1082   // the chassis behind everything
#define C_GLASS    0x18C4   // inside the tube: barely lighter than the case
#define C_GEDGE    0x4209   // the glass envelope's edge
#define C_LIT      0xFDA7   // the discharge itself, near white-hot at the core
#define C_GLOW     0xFB62   // its orange body
#define C_HALO     0x7961   // the spill onto the glass around it
#define C_DARK     0x39A6   // an unlit cathode, catching a little light
#define C_MESH     0x2945   // the anode mesh in front of the stack
#define C_PIN      0x7B8C   // base pins
#define C_LABEL    0x93C8   // printing on the chassis
#define C_NEON     0xFB62   // the colon neons

// ---- geometry --------------------------------------------------------------
#define TUBE_W     44       // glass envelope
#define TUBE_H     78
#define TUBE_Y     46
#define DIG_W      22       // the numeral inside it
#define DIG_H      38

uint16_t faceBackground() { return C_CASE; }
bool     faceSmooth()     { return false; }   // the neons blink on the second
void     faceInit()       { }

// ---- the numerals ----------------------------------------------------------
// Each digit is a run of strokes in a 3x5 grid of points, which is how a bent
// wire cathode actually reads: no closed segments, and the diagonals of a 7
// or a 4 are real diagonals rather than staircases.
//
//   0 1 2      points are (col, row), origin top-left
//   3 4 5
//   6 7 8
//   9 A B
//   C D E
struct Stroke { uint8_t a, b; };
static const Stroke S0[] = {{0,2},{2,14},{14,12},{12,0}};
static const Stroke S1[] = {{1,13}};
static const Stroke S2[] = {{0,2},{2,8},{8,6},{6,12},{12,14}};
static const Stroke S3[] = {{0,2},{2,14},{14,12},{6,8}};
static const Stroke S4[] = {{0,6},{6,8},{2,14}};
static const Stroke S5[] = {{2,0},{0,6},{6,8},{8,14},{14,12}};
static const Stroke S6[] = {{2,0},{0,12},{12,14},{14,8},{8,6}};
static const Stroke S7[] = {{0,2},{2,12}};
static const Stroke S8[] = {{0,2},{2,14},{14,12},{12,0},{6,8}};
static const Stroke S9[] = {{8,6},{6,0},{0,2},{2,14},{14,12}};

struct Glyph { const Stroke *s; uint8_t n; };
static const Glyph GLYPH[10] = {
    {S0, 4}, {S1, 1}, {S2, 5}, {S3, 4}, {S4, 3},
    {S5, 5}, {S6, 5}, {S7, 2}, {S8, 5}, {S9, 5},
};

// A grid point in tube-local coordinates.
static inline void pt(int idx, int x, int y, int w, int h, float &px, float &py)
{
    px = x + (idx % 3) * (w / 2.0f);
    py = y + (idx / 3) * (h / 4.0f);
}

template <typename GFX>
static void numeral(GFX &g, int d, int x, int y, int w, int h,
                    float weight, uint16_t col, uint16_t bg)
{
    if (d < 0 || d > 9) return;
    const Glyph &gl = GLYPH[d];
    for (int i = 0; i < gl.n; i++) {
        float ax, ay, bx, by;
        pt(gl.s[i].a, x, y, w, h, ax, ay);
        pt(gl.s[i].b, x, y, w, h, bx, by);
        g.drawWideLine(ax, ay, bx, by, weight, col, bg);
    }
}

// ---- a tube ----------------------------------------------------------------
// The lit digit, the dark cathodes stacked around it, the halo, and the glass.
template <typename GFX>
static void tube(GFX &g, int cx, int digit)
{
    const int x = cx - TUBE_W / 2, y = TUBE_Y;

    // glass envelope: a rounded capsule, slightly lighter than the chassis
    g.fillRoundRect(x, y, TUBE_W, TUBE_H, 10, C_GLASS);
    g.drawRoundRect(x, y, TUBE_W, TUBE_H, 10, C_GEDGE);

    const int dx = cx - DIG_W / 2;
    const int dy = y + (TUBE_H - DIG_H) / 2 - 2;

    // The unlit cathodes: two of them, offset back and to the side, thin and
    // dark. They are what makes the tube read as a stack of wires rather than
    // a printed number.
    numeral(g, (digit + 7) % 10, dx + 3, dy + 3, DIG_W, DIG_H, 1.6f, C_DARK, C_GLASS);
    numeral(g, (digit + 4) % 10, dx - 2, dy + 1, DIG_W, DIG_H, 1.6f, C_DARK, C_GLASS);

    // The discharge, in three passes: a wide soft halo, the orange body, and
    // a hot core. Drawing it as three widths is what gives the glow its
    // falloff without any per-pixel blending.
    numeral(g, digit, dx, dy, DIG_W, DIG_H, 8.0f, C_HALO, C_GLASS);
    numeral(g, digit, dx, dy, DIG_W, DIG_H, 4.5f, C_GLOW, C_HALO);
    numeral(g, digit, dx, dy, DIG_W, DIG_H, 2.0f, C_LIT,  C_GLOW);

    // The anode mesh sits in front of the whole stack: fine vertical wires.
    for (int i = x + 6; i < x + TUBE_W - 5; i += 5)
        g.drawFastVLine(i, y + 8, TUBE_H - 16, C_MESH);

    // base pins, just visible below the glass
    for (int i = 0; i < 4; i++)
        g.fillRect(x + 9 + i * 8, y + TUBE_H, 3, 5, C_PIN);
}

// ---- the face --------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;
    g.fillScreen(C_CASE);

    // 24 hour, like every digital face here.
    tube(g, 44,  t.tm_hour / 10);
    tube(g, 92,  t.tm_hour % 10);
    tube(g, 148, t.tm_min  / 10);
    tube(g, 196, t.tm_min  % 10);

    // The colon neons between the pairs, flashing on the second as the real
    // ones do. Off is the dark cathode colour, not black: a neon that is not
    // struck still catches the light.
    {
        uint16_t c = (t.tm_sec & 1) ? C_NEON : C_DARK;
        g.fillSmoothCircle(120, TUBE_Y + 26, 4, c, C_CASE);
        g.fillSmoothCircle(120, TUBE_Y + 52, 4, c, C_CASE);
        if (t.tm_sec & 1) {
            g.fillSmoothCircle(120, TUBE_Y + 26, 7, C_HALO, C_CASE);
            g.fillSmoothCircle(120, TUBE_Y + 26, 4, c,      C_HALO);
            g.fillSmoothCircle(120, TUBE_Y + 52, 7, C_HALO, C_CASE);
            g.fillSmoothCircle(120, TUBE_Y + 52, 4, c,      C_HALO);
        }
    }

    // The chassis printing: date on the left, weather on the right, with a
    // rule between them. Small caps, the way instrument panels are labelled.
    static const char *WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char *MO[12] = { "JAN","FEB","MAR","APR","MAY","JUN",
                                  "JUL","AUG","SEP","OCT","NOV","DEC" };
    char buf[24];

    g.drawFastHLine(38, 146, 164, C_GEDGE);

    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_LABEL, C_CASE);
    snprintf(buf, sizeof buf, "%s %02d %s", WD[t.tm_wday % 7], t.tm_mday,
             MO[t.tm_mon % 12]);
    g.drawString(buf, CX, 162, 2);

    if (wxValid) snprintf(buf, sizeof buf, "%d%c%s", wxTempF, 0xB0, wxUnit());
    else         strcpy(buf, "--");
    g.setTextColor(C_GLOW, C_CASE);
    g.drawString(buf, CX, 186, 2);

    // The seconds, small and printed rather than tubed - four more tubes will
    // not fit, and a nixie clock with a seconds readout usually has it in a
    // smaller size anyway.
    snprintf(buf, sizeof buf, "%02d", t.tm_sec);
    g.setTextColor(C_LABEL, C_CASE);
    g.drawString(buf, CX, 208, 2);

    // Status: a small neon by the seconds, green when the clock is synced.
    g.fillSmoothCircle(CX + 26, 208, 3, statusCol, C_CASE);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_nixie

const FaceVTable FACE_NIXIE = {
    "nixie",
    face_nixie::faceInit,
    face_nixie::faceBackground,
    face_nixie::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_nixie::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_nixie::faceRender,
};
