// ---------------------------------------------------------------------------
//  Face: "pcb"  -  bare board with four DIP LED modules, seen through red glass
//
//  The watch this copies is a black PCB under a red crystal with four vintage
//  DIP seven-segment LED modules soldered across the middle. What sells it is
//  not the digits, it is everything around them: gold pins top and bottom of
//  each module, a bubble-lens window with the segment shadows inside, SMD
//  resistors and ICs with their reference designators printed beside them,
//  traces running to vias, a DIP switch labelled AM/PM, and four indicator
//  LEDs in the corners throwing red halos across the board. Leave any of that
//  out and it turns into an icon of a circuit board; keep it and it reads as
//  hardware.
//
//  All of it is drawn with plain primitives. A PCB is rectangles, lines and
//  circles by nature, so this is the one place where "true to life" and
//  "cheap to draw" agree.
//
//  Colours are chosen as if lit through red glass: white silkscreen goes
//  pink-grey, gold pins go orange, the black solder mask goes deep maroon.
//  Nothing on the face is pure white or neutral grey.
// ---------------------------------------------------------------------------
#include "../face.h"

namespace face_pcb {

// ---- palette (everything under red glass) ---------------------------------
#define C_CASE      0x0000   // outside the board
#define C_PCB       0x1000   // black solder mask, maroon through the glass
#define C_PCB_EDGE  0x3000
#define C_TRACE     0x3000
#define C_VIA       0x4800
#define C_SILK      0x9CD3   // white silkscreen, dulled
#define C_SILK_DIM  0x6B4D
#define C_MOD       0x2800   // module body
#define C_MOD_EDGE  0x5000
#define C_LENS      0x5000   // bubble lens window
#define C_GHOST     0x6800   // unlit segment shadow inside the lens
#define C_SEG       0xF800   // lit segment
#define C_SEG_GLOW  0xA000
#define C_PIN       0xFB40   // gold pins, orange under the glass
#define C_PIN_DK    0x8200
#define C_LED       0xF800
#define C_LED_HALO  0x3000
#define C_LED_HALO2 0x6800
#define C_LED_HI    0xFDB6   // specular dot on the LED dome
#define C_SMD       0x2104   // resistor body
#define C_SMD_CAP   0xB596   // metal end caps
#define C_CERAMIC   0xA4A5   // capacitor
#define C_IC        0x18E3
#define C_SWITCH    0xB000   // DIP switch body, red
#define C_SLIDER    0xE71C   // its white sliders, pinkish
#define C_PAD       0xB596

// ---- module geometry ------------------------------------------------------
#define MOD_W    34
#define MOD_H    46
#define MOD_GAP   8
#define MOD_X0   40
#define MOD_Y    97
#define PIN_N     5
#define PIN_LEN   8
#define BUS_TOP  84
#define BUS_BOT 154

static inline int modX(int i) { return MOD_X0 + i * (MOD_W + MOD_GAP); }

uint16_t faceBackground() { return C_CASE; }
bool     faceSmooth()     { return false; }
void     faceInit()       { }

//   bit: a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40
static const uint8_t SEG_MAP[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// Seven segments; `on` for lit, `off` for the unlit shadow, or 0 to skip.
template <typename GFX>
static void seg7(GFX &g, int x, int y, int w, int h, int t, int d,
                 uint16_t on, uint16_t off, bool drawOff)
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
    for (int i = 0; i < 7; i++) {
        if (m & s[i].bit)  g.fillRect(s[i].x, s[i].y, s[i].w, s[i].h, on);
        else if (drawOff)  g.fillRect(s[i].x, s[i].y, s[i].w, s[i].h, off);
    }
}

// ---- pieces of the board --------------------------------------------------
template <typename GFX>
static void via(GFX &g, int x, int y)
{
    g.fillCircle(x, y, 3, C_VIA);
    g.fillCircle(x, y, 1, C_PCB);          // the hole
}

template <typename GFX>
static void resistor(GFX &g, int x, int y, const char *ref, int refX)
{
    g.fillRect(x, y, 14, 6, C_SMD);
    g.fillRect(x, y, 3, 6, C_SMD_CAP);
    g.fillRect(x + 11, y, 3, 6, C_SMD_CAP);
    g.setTextDatum(TL_DATUM);
    g.setTextColor(C_SILK_DIM, C_PCB);
    g.drawString(ref, refX, y - 1, 1);
}

template <typename GFX>
static void capacitor(GFX &g, int x, int y)
{
    g.fillRect(x, y, 8, 5, C_CERAMIC);
    g.fillRect(x, y, 2, 5, C_SMD_CAP);
    g.fillRect(x + 6, y, 2, 5, C_SMD_CAP);
}

template <typename GFX>
static void ic(GFX &g, int x, int y, const char *ref, int refX)
{
    g.fillRect(x, y, 22, 12, C_IC);
    g.drawRect(x, y, 22, 12, C_MOD_EDGE);
    for (int k = 0; k < 4; k++) {                  // four pins each side
        g.fillRect(x + 3 + k * 5, y - 3, 2, 3, C_PIN_DK);
        g.fillRect(x + 3 + k * 5, y + 12, 2, 3, C_PIN_DK);
    }
    g.fillCircle(x + 3, y + 3, 1, C_SILK_DIM);     // pin-1 dot
    g.setTextDatum(TL_DATUM);
    g.setTextColor(C_SILK_DIM, C_PCB);
    g.drawString(ref, refX, y + 2, 1);
}

// An indicator LED in a corner: a wide soft halo, a tighter bright one, the
// dome, and a specular dot. The halos are what light the rest of the board.
template <typename GFX>
static void cornerLed(GFX &g, int x, int y)
{
    g.fillSmoothCircle(x, y, 16, C_LED_HALO,  C_PCB);
    g.fillSmoothCircle(x, y,  9, C_LED_HALO2, C_LED_HALO);
    g.fillSmoothCircle(x, y,  4, C_LED,       C_LED_HALO2);
    g.fillSmoothCircle(x - 1, y - 1, 1, C_LED_HI, C_LED);
}

template <typename GFX>
static void module(GFX &g, int i, int digit)
{
    const int mx = modX(i), my = MOD_Y;

    // gold pins, above and below, each with a dark edge for a little depth
    for (int k = 0; k < PIN_N; k++) {
        int px = mx + 4 + k * 6;
        g.fillRect(px - 1, my - PIN_LEN - 1, 1, PIN_LEN, C_PIN_DK);
        g.fillRect(px,     my - PIN_LEN - 1, 2, PIN_LEN, C_PIN);
        g.fillRect(px - 1, my + MOD_H + 1,   1, PIN_LEN, C_PIN_DK);
        g.fillRect(px,     my + MOD_H + 1,   2, PIN_LEN, C_PIN);
    }

    // package body and the lens window in its face
    g.fillRoundRect(mx, my, MOD_W, MOD_H, 3, C_MOD);
    g.drawRoundRect(mx, my, MOD_W, MOD_H, 3, C_MOD_EDGE);
    g.fillRect(mx + 4, my + 5, 26, 36, C_LENS);

    // unlit segments show faintly through a real lens, then the lit ones
    // with a one-step glow so they read as emitters rather than paint
    seg7(g, mx + 7, my + 8, 20, 30, 4, digit, C_SEG, C_GHOST, true);
    seg7(g, mx + 6, my + 7, 22, 32, 6, digit, C_SEG_GLOW, 0, false);
    seg7(g, mx + 7, my + 8, 20, 30, 4, digit, C_SEG, 0, false);
}

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;

    g.fillScreen(C_CASE);

    // the board itself, sitting inside the case with a lit edge
    g.fillSmoothCircle(120, 120, 112, C_PCB, C_CASE);
    g.drawSmoothCircle(120, 120, 112, C_PCB_EDGE, C_CASE);

    // ---- copper: buses, stubs to every pin, side runs, a few diagonals ----
    g.drawFastHLine(44, BUS_TOP, 152, C_TRACE);
    g.drawFastHLine(44, BUS_BOT, 152, C_TRACE);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < PIN_N; k++) {
            int px = modX(i) + 4 + k * 6;
            g.drawFastVLine(px, BUS_TOP, MOD_Y - PIN_LEN - 1 - BUS_TOP, C_TRACE);
            g.drawFastVLine(px, MOD_Y + MOD_H + PIN_LEN + 1,
                            BUS_BOT - (MOD_Y + MOD_H + PIN_LEN + 1), C_TRACE);
        }
    // from the corner LEDs to the buses
    g.drawFastVLine(44,  60, BUS_TOP - 60, C_TRACE);
    g.drawFastVLine(196, 60, BUS_TOP - 60, C_TRACE);
    g.drawFastVLine(44,  BUS_BOT, 180 - BUS_BOT, C_TRACE);
    g.drawFastVLine(196, BUS_BOT, 180 - BUS_BOT, C_TRACE);
    // outboard runs that disappear under the outer modules
    g.drawFastVLine(28,  110, 20, C_TRACE);  g.drawFastHLine(28,  120, 12, C_TRACE);
    g.drawFastVLine(212, 110, 20, C_TRACE);  g.drawFastHLine(200, 120, 12, C_TRACE);
    // the 45-degree jogs a router always leaves behind
    g.drawFastVLine(96,  49, 11, C_TRACE); g.drawLine(96, 60, 104, 68, C_TRACE);
    g.drawFastVLine(104, 68, BUS_TOP - 68, C_TRACE);
    g.drawFastVLine(144, 49, 11, C_TRACE); g.drawLine(144, 60, 136, 68, C_TRACE);
    g.drawFastVLine(136, 68, BUS_TOP - 68, C_TRACE);
    g.drawFastVLine(75,  76, BUS_TOP - 76, C_TRACE);
    g.drawFastVLine(151, 76, BUS_TOP - 76, C_TRACE);
    g.drawFastVLine(120, BUS_BOT, 14, C_TRACE);
    g.drawFastHLine(96,  177, 11, C_TRACE);
    g.drawFastHLine(133, 177, 11, C_TRACE);

    static const int VIAS[][2] = {
        {44, BUS_TOP}, {196, BUS_TOP}, {44, BUS_BOT}, {196, BUS_BOT},
        {28, 110}, {28, 130}, {212, 110}, {212, 130},
        {96, 49}, {144, 49}, {104, 68}, {136, 68},
    };
    for (auto &v : VIAS) via(g, v[0], v[1]);

    // ---- silkscreen ------------------------------------------------------
    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_SILK_DIM, C_PCB);
    g.drawString("2609CLK-4D-0042", 120, 22, 1);       // batch / serial line
    g.setTextDatum(TL_DATUM);
    g.setTextColor(C_SILK, C_PCB);
    g.drawString("CHR", 64, 34, 1);
    g.setTextDatum(TC_DATUM);
    g.drawString("AM/PM", 120, 158, 1);
    g.setTextColor(C_SILK_DIM, C_PCB);
    g.drawString("REV A", 120, 204, 1);

    // ---- passives and ICs, top cluster -----------------------------------
    resistor(g, 64, 44, "R1", 80);
    resistor(g, 64, 54, "R2", 80);
    resistor(g, 150, 44, "R3", 166);
    capacitor(g, 100, 44);
    capacitor(g, 112, 44);
    g.fillRect(150, 55, 12, 6, C_SMD_CAP);              // crystal can
    g.setTextDatum(TL_DATUM);
    g.setTextColor(C_SILK_DIM, C_PCB);
    g.drawString("Y1", 166, 54, 1);
    ic(g, 64,  64, "U1", 90);
    ic(g, 140, 64, "U2", 166);

    // ---- DIP switch and its pads, bottom cluster -------------------------
    g.fillRect(107, 168, 26, 18, C_SWITCH);
    g.drawRect(107, 168, 26, 18, C_MOD_EDGE);
    for (int k = 0; k < 4; k++)
        g.fillRect(110 + k * 6, 173, 4, 8, C_SLIDER);
    g.fillRect(90,  172, 6, 6, C_PAD);
    g.fillRect(144, 172, 6, 6, C_PAD);
    g.setTextColor(C_SILK_DIM, C_PCB);
    g.drawString("0", 80,  171, 1);
    g.drawString("0", 152, 171, 1);

    // ---- corner indicator LEDs, before the modules so their halos sit
    //      underneath the packages the way light would --------------------
    cornerLed(g, 44,  52);
    cornerLed(g, 196, 52);
    cornerLed(g, 44,  188);
    cornerLed(g, 196, 188);

    // ---- the four modules, HH MM ----------------------------------------
    module(g, 0, t.tm_hour / 10);
    module(g, 1, t.tm_hour % 10);
    module(g, 2, t.tm_min  / 10);
    module(g, 3, t.tm_min  % 10);

    // ---- separator LED between the pairs, blinking on the second --------
    if (t.tm_sec & 1) {
        g.fillSmoothCircle(120, 120, 7, C_LED_HALO2, C_PCB);
        g.fillSmoothCircle(120, 120, 3, C_LED, C_LED_HALO2);
    } else {
        g.fillSmoothCircle(120, 120, 3, C_LED_HALO, C_PCB);   // dome, unlit
    }
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_pcb

const FaceVTable FACE_PCB = {
    "pcb",
    face_pcb::faceInit,
    face_pcb::faceBackground,
    face_pcb::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_pcb::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_pcb::faceRender,
};
