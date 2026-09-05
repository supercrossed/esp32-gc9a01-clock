// ---------------------------------------------------------------------------
//  Face: "dotmatrix"  -  1970s red LED watch, dot array
//
//  The other construction: a physical grid of round emitters behind red glass.
//  Its charm is the thing the segment version does not have - the UNLIT dots
//  stay faintly visible, so you can see the whole array sitting there even
//  where nothing is on. That dark-red field of dormant dots is what makes the
//  third reference photo read as a real object rather than as a display.
//
//  So the array is drawn in full, every frame, and the digits are just which
//  dots are bright. No glow and no falloff: an LED is either on or it is not.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <string.h>

// Wrapped in its own namespace so every face can be linked into the same
// binary: each defines faceInit/faceRender/faceBackground/faceSmooth, which
// would otherwise be six duplicate global symbols.
namespace face_dotmatrix {

// ---- palette --------------------------------------------------------------
#define C_GLASS   0x0000   // black filter
#define C_OFF     0x2000   // dormant emitter, just visible through the glass
#define C_ON      0xF842   // lit
#define C_BLOOM   0x7800   // one step of bleed around a lit dot
#define C_LABEL   0x7000
#define C_TEXT    0xB000

// ---- the array ------------------------------------------------------------
// HH stacked over MM, not four digits in a row.
//
// A single row of four is the wrong shape for a round dial: it must be wide
// enough for four 5-wide numerals side by side, yet it is only 7 rows tall,
// so it sprawls horizontally while leaving the top and bottom of the dial
// empty. Stacking uses the height instead - the array narrows from 184 px to
// 132 px AND the dots grow, because the pitch can go from 8 to 12.
//
//   11 cols = 5 + gap + 5      15 rows = 7 + gap + 7
//
// The DIGITS occupy that 11x15 block, but the dot field itself runs to the
// edge of the glass. A real dot-matrix watch is a full panel of emitters with
// the numerals lit somewhere inside it - stopping the dormant dots at the
// edge of the digit block leaves a visible rectangle on a round dial, which
// reads as a sticker rather than a display. 21 columns is the first odd count
// whose half-span (126 px) clears the 114 px cull radius, so the field fills
// the dial and the circle does the cropping.
#define DIG_COLS 11
#define DIG_ROWS 15
#define COLS   21
#define ROWS   21
#define PITCH  12          // centre-to-centre
#define DOT_R   4          // drawn radius

// Where the digit block sits inside the larger field.
#define DIG_C0 ((COLS - DIG_COLS) / 2)
#define DIG_R0 ((ROWS - DIG_ROWS) / 2)

#define GRID_W (COLS * PITCH)
#define GRID_H (ROWS * PITCH)
#define GRID_X (120 - GRID_W / 2 + PITCH / 2)
#define GRID_Y (120 - GRID_H / 2 + PITCH / 2)

uint16_t faceBackground() { return C_GLASS; }
bool     faceSmooth()     { return false; }
void     faceInit()       { }

// ---- 5x7 numerals ---------------------------------------------------------
// Five bits per row, MSB leftmost.
static const uint8_t FONT[10][ROWS] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},   // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},   // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},   // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},   // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},   // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},   // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},   // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},   // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},   // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},   // 9
};

// Two digits per row, one blank column between; one blank row between rows.
// Offset into the full field so the numerals stay centred on the dial while
// the dormant dots run past them to the glass.
#define DL_C  (DIG_C0 + 0) // left digit column
#define DR_C  (DIG_C0 + 6) // right digit column
#define TOP_R (DIG_R0 + 0) // hours row
#define BOT_R (DIG_R0 + 8) // minutes row

static void stamp(uint8_t *m, int d, int c0, int r0)
{
    if (d < 0 || d > 9) return;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (FONT[d][r] & (0x10 >> c))
                m[(r0 + r) * COLS + c0 + c] = 1;
}

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    // Hard pixels: this face is imitating a cheap LCD, and smoothing the
    // type would erase the very thing being imitated.
    textSmooth(g, false);
    (void)subSec;
    char buf[16];

    uint8_t on[COLS * ROWS];
    memset(on, 0, sizeof(on));
    stamp(on, (t.tm_hour / 10) % 10, DL_C, TOP_R);
    stamp(on,  t.tm_hour % 10,       DR_C, TOP_R);
    stamp(on, (t.tm_min  / 10) % 10, DL_C, BOT_R);
    stamp(on,  t.tm_min  % 10,       DR_C, BOT_R);
    // separator: two dots on the blank row between hours and minutes,
    // blinking on the second
    if (t.tm_sec & 1) {
        on[(DIG_R0 + 7) * COLS + DIG_C0 + 2] = 1;
        on[(DIG_R0 + 7) * COLS + DIG_C0 + 8] = 1;
    }

    g.fillScreen(C_GLASS);

    // The whole array, lit and dormant alike. Drawing the unlit dots is the
    // entire point - it is what shows the display as a physical grid.
    for (int r = 0; r < ROWS; r++) {
        int y = GRID_Y + r * PITCH;
        for (int c = 0; c < COLS; c++) {
            int x = GRID_X + c * PITCH;
            // Cull on the dot's OUTER EDGE, not its centre: a centre-only
            // test lets the body of an edge dot hang over the bezel.
            int dx = x - 120, dy = y - 120;
            int lim = 119 - (DOT_R + 1);
            if (dx * dx + dy * dy > lim * lim) continue;

            if (on[r * COLS + c]) {
                g.fillSmoothCircle(x, y, DOT_R + 1, C_BLOOM, C_GLASS);
                g.fillSmoothCircle(x, y, DOT_R,     C_ON,    C_GLASS);
            } else {
                g.fillSmoothCircle(x, y, DOT_R - 1, C_OFF,   C_GLASS);
            }
        }
    }

    // captions outside the array
    static const char *DAYS[7] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
    snprintf(buf, sizeof(buf), "%s %d", DAYS[t.tm_wday % 7], t.tm_mday);
    g.setTextDatum(TC_DATUM);
    g.setTextColor(timeValid ? C_TEXT : C_LABEL, C_GLASS);
    g.drawString(buf, 120, 214, 2);

    if (wxValid) {
        // Font 4 rather than 2: at the previous size it read as a footnote
        // next to 63 px of digits. Sits at y=6, not 12: font 4 is 26 px tall
        // and the top dot row starts at y=33, so anything lower runs into it.
        snprintf(buf, sizeof(buf), "%d", wxTempF);
        g.setTextDatum(TC_DATUM);
        g.setTextColor(C_TEXT, C_GLASS);
        g.drawString(buf, 112, 6, 4);
        int w = g.textWidth(buf, 4);
        g.drawCircle(112 + w / 2 + 8, 11, 3, C_TEXT);
        g.setTextDatum(TL_DATUM);
        g.drawString(wxUnit(), 112 + w / 2 + 14, 12, 2);
    }
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_dotmatrix

// Registration: the one symbol this file exposes.
const FaceVTable FACE_DOTMATRIX = {
    "dotmatrix",
    face_dotmatrix::faceInit,
    face_dotmatrix::faceBackground,
    face_dotmatrix::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_dotmatrix::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_dotmatrix::faceRender,
};
