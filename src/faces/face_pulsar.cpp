// ---------------------------------------------------------------------------
//  Face: "pulsar"  -  early-70s LED watch, dot-built segments
//
//  What makes a Pulsar P2 or a Kingsonic look the way it does is not the
//  seven-segment shape. It is that every segment is a short row of tiny
//  discrete LEDs, each glowing through a dark red filter with a soft halo.
//  Draw solid bars and you get a 1980s calculator; draw the dots and you get
//  1973. So this face never draws a bar. A digit is a 5x7 field of LED
//  positions, a segment is the subset of positions along its path, corners
//  are shared, and every lit position is a small bright core sitting in a
//  haze just wide enough to merge with its neighbours' - which is where the
//  slightly blurred glow of the real thing comes from.
//
//  Everything else is black. These watches showed nothing but the time, and
//  the empty black window around small digits is half of the look.
//
//  24-hour, with the leading zero blanked below ten o'clock, as the originals
//  did with their single hour digit.
// ---------------------------------------------------------------------------
#include "../face.h"

namespace face_pulsar {

#define C_BG    0x0000
#define C_CORE0 0xF800   // the LED itself. Three levels, because a real array
#define C_CORE1 0xE000   // was never perfectly even and a uniform one reads
#define C_CORE2 0xD000   // as a rendering rather than as hardware.
#define C_RING  0x8000   // filter glow immediately around each LED
#define C_HAZE  0x3000   // faint spill that merges neighbours into a bar

#define P       8        // LED pitch, px
#define R_CORE  2
#define R_RING  3
#define R_HAZE  4

// ---- layout ---------------------------------------------------------------
// Four digits and a colon, small, centred, with a lot of black around them.
#define DIG_W   (4 * P)                        // five columns span four pitches
#define DIG_H   (6 * P)                        // seven rows span six
// Digits are separated by one empty LED cell (2 * P), so the space between
// them is visibly wider than the pitch inside a digit. Tighter than that and
// the outer columns of neighbouring digits merge into one bar.
#define GAP     16                             // between digits of a pair
#define COLON_W 24                             // pair gap: empty, colon, empty
#define X0      28                             // centres the 184 px row
#define X1      (X0 + DIG_W + GAP)
#define XC      (X1 + DIG_W + COLON_W / 2)     // colon column, = 120
#define X2      (X1 + DIG_W + COLON_W)
#define X3      (X2 + DIG_W + GAP)
#define Y0      (120 - DIG_H / 2)

//   bit: a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40
static const uint8_t SEG_MAP[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// LED positions (col,row) each segment occupies. Corners appear in two lists
// and are lit once; the field below deduplicates them.
struct CR { uint8_t c, r; };
static const CR SEG_A[] = {{0,0},{1,0},{2,0},{3,0},{4,0}};
static const CR SEG_G[] = {{0,3},{1,3},{2,3},{3,3},{4,3}};
static const CR SEG_D[] = {{0,6},{1,6},{2,6},{3,6},{4,6}};
static const CR SEG_F[] = {{0,0},{0,1},{0,2},{0,3}};
static const CR SEG_B[] = {{4,0},{4,1},{4,2},{4,3}};
static const CR SEG_E[] = {{0,3},{0,4},{0,5},{0,6}};
static const CR SEG_C[] = {{4,3},{4,4},{4,5},{4,6}};

uint16_t faceBackground() { return C_BG; }
bool     faceSmooth()     { return false; }
void     faceInit()       { }

struct Led { int16_t x, y; uint8_t lvl; };

static void light(bool f[7][5], const CR *s, int n)
{
    for (int i = 0; i < n; i++) f[s[i].r][s[i].c] = true;
}

// Append every LED of digit d, whose column 0 is at x0. d < 0 draws nothing,
// which is how the leading zero is blanked.
static void collectDigit(Led *leds, int &n, int d, int x0)
{
    if (d < 0 || d > 9) return;
    bool f[7][5] = {};
    uint8_t m = SEG_MAP[d];
    if (m & 0x01) light(f, SEG_A, 5);
    if (m & 0x02) light(f, SEG_B, 4);
    if (m & 0x04) light(f, SEG_C, 4);
    if (m & 0x08) light(f, SEG_D, 5);
    if (m & 0x10) light(f, SEG_E, 4);
    if (m & 0x20) light(f, SEG_F, 4);
    if (m & 0x40) light(f, SEG_G, 5);
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (f[r][c])
                leds[n++] = { (int16_t)(x0 + c * P), (int16_t)(Y0 + r * P),
                              (uint8_t)((c * 7 + r * 13 + x0) % 3) };
}

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;
    static const uint16_t CORE[3] = { C_CORE0, C_CORE1, C_CORE2 };

    Led leds[4 * 35 + 2];
    int n = 0;

    int hh = t.tm_hour, mm = t.tm_min;
    collectDigit(leds, n, hh >= 10 ? hh / 10 : -1, X0);   // blank below ten
    collectDigit(leds, n, hh % 10,                  X1);
    collectDigit(leds, n, mm / 10,                  X2);
    collectDigit(leds, n, mm % 10,                  X3);

    // colon: two LEDs, blinking on the second so the face is visibly alive
    // even though it shows no seconds
    if (t.tm_sec & 1) {
        leds[n++] = { (int16_t)XC, (int16_t)(Y0 + 2 * P), 0 };
        leds[n++] = { (int16_t)XC, (int16_t)(Y0 + 4 * P), 0 };
    }

    g.fillScreen(C_BG);

    // Three passes over the whole set, widest first, so a neighbour's haze
    // never paints over an already-drawn core. Each layer blends against the
    // layer beneath it, not against black, or the rings would punch dark
    // rims into the haze.
    for (int i = 0; i < n; i++)
        g.fillSmoothCircle(leds[i].x, leds[i].y, R_HAZE, C_HAZE, C_BG);
    for (int i = 0; i < n; i++)
        g.fillSmoothCircle(leds[i].x, leds[i].y, R_RING, C_RING, C_HAZE);
    for (int i = 0; i < n; i++)
        g.fillSmoothCircle(leds[i].x, leds[i].y, R_CORE, CORE[leds[i].lvl], C_RING);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_pulsar

const FaceVTable FACE_PULSAR = {
    "pulsar",
    face_pulsar::faceInit,
    face_pulsar::faceBackground,
    face_pulsar::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_pulsar::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_pulsar::faceRender,
};
