// ---------------------------------------------------------------------------
//  Face: "mosaic"  -  pixel-tile clock with a live pastel background
//
//  The whole dial is a fine grid of square tiles. Every tile lives in a pastel
//  HSV space - low saturation, high value - and does two things at once:
//
//    * drifts its hue a short distance to a nearby hue, then picks another
//      nearby target. The step is small by construction, so a tile only ever
//      slides to a neighbouring colour rather than jumping across the wheel.
//    * breathes, its brightness riding a slow sine at its own period.
//
//  Both run at per-tile rates and phases, so the field is never in step and
//  never settles into a visible loop - it just stays quietly alive.
//
//  The time sits in the middle as 24-hour HH over MM, drawn by claiming tiles
//  out of the grid and filling them solid, so the digits read as part of the
//  mosaic rather than as text laid on top of it.
//
//  Tiles run to the edge of the framebuffer; the round bezel crops them, which
//  is what gives the cut-off tiles around the rim.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <string.h>

// Wrapped in its own namespace so every face can be linked into the same
// binary: each defines faceInit/faceRender/faceBackground/faceSmooth, which
// would otherwise be six duplicate global symbols.
namespace face_mosaic {

// ---- grid -----------------------------------------------------------------
// 17 columns with the origin pulled back half a cell, rather than 16 flush
// to the edge. The digit block is 9 cells wide, and 9 cannot sit centred in
// 16 - it lands half a cell out whichever column it starts on, which reads as
// the time being up and to the left. Offsetting the whole grid by -7 fixes
// that, and the 17th row/column keeps the far edges covered (the grid spans
// -7..246, so the visible 0..240 is still full-bleed).
// The grid is drawn oversized and slid each frame so the time's *lit* tiles
// land dead centre (see shiftX/shiftY). Digits and background move together,
// so they stay in register; the spare rows and columns are what keep the
// edges covered at the extremes of that slide.
#define GRID   18
#define CELL   15
#define TILE   13          // leaves a 2 px gutter
#define GRID_OFF (-15)     // base origin, before the centring slide
#define NCELL  (GRID * GRID)

// Digit block: 4x5 tiles per digit, HH above MM. At this cell size a 5x7
// digit would put the block's corners at radius 139 on a 120 px display and
// clip badly; 4x5 puts the furthest corner at 105.9.
// 5 wide, not 4: a "1" is a 3-column glyph, and 3 cannot sit centred in a
// 4-column cell - it lands half a cell right, which shifted the whole time
// 7 px whenever an hour or minute began with 1. At 5 columns every glyph
// centres on the middle column, so the ink centre is the same for all times.
#define D_W    5
#define D_H    5
#define COL0   4           // first digit column
#define COLGAP 6           // second digit column offset (5 wide + 1 gutter)
#define ROWTOP 4
#define ROWBOT 10

// ---- pastel colour space --------------------------------------------------
// Saturation stays low and value stays high, which is what keeps every colour
// a pastel no matter where its hue wanders to.
// Day: pale, washed-out, hues anywhere on the wheel.
#define DAY_SAT    72      // of 255
#define DAY_VAL   200
#define DAY_AMP    26      // breathing depth
// Night: darker, deeper, and squeezed into the blues. This panel's backlight
// has no dimming pin, so lowering the pixel values is the only brightness
// control available - a night palette is that control as well as a look.
#define NIGHT_SAT 150
#define NIGHT_VAL  70
#define NIGHT_AMP  14
#define NIGHT_HUE_LO 140   // ~198 deg, cyan-blue
#define NIGHT_HUE_HI 185   // ~261 deg, blue-violet
#define MIX_STEP     1     // ~10 s to cross over, so dusk is a fade not a jump
#define HUE_STEP   26      // largest hue jump per target - keeps drifts local
#define RATE_MIN    3      // hue crossfade speed; each tile re-rolls its own
#define RATE_SPAN   6      // rate on every hop, so no two stay in step

#define C_GUTTER   0x0000   // between tiles
// Digits invert across the day: near-black on the pale day field, pale on
// the dark night one - near-black digits would vanish into a night tile.
#define DIGIT_DAY_R   24
#define DIGIT_DAY_G   28
#define DIGIT_DAY_B   24
#define DIGIT_NGT_R  110
#define DIGIT_NGT_G  135
#define DIGIT_NGT_B  185
#define C_DIGIT_NG 0x6180   // digit fill while the clock is not NTP-synced

// ---- per-tile state -------------------------------------------------------
static uint8_t tHue[NCELL], tHueTo[NCELL], tProg[NCELL], tRate[NCELL];
static uint8_t tPhase[NCELL], tPhRate[NCELL];
static uint8_t nightMix = 0;   // 0 = full day, 255 = full night
static int     shiftX = 0, shiftY = 0;   // eased centring slide, in pixels

// render() runs once a frame on a full-framebuffer board, but the AMOLED
// renders in horizontal bands and calls it once per band - eight times for a
// full frame, and again for every dirty box. Anything that steps the
// animation therefore cannot live in the body of render(): the field would
// run eight times too fast, and because the steps land between bands, each
// band would be drawn from a different state and the grid would shear.
// This fires once per distinct (time, sub-second) instead.
static bool     haveFrame = false;
static time_t   lastFrameSec = 0;
static float    lastFrameSub = -1.0f;
static uint32_t lastStepMs = 0;

static bool newFrame(const struct tm &t, float sub)
{
    time_t key = (time_t)t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    if (haveFrame && key == lastFrameSec && sub == lastFrameSub) return false;
    haveFrame = true; lastFrameSec = key; lastFrameSub = sub;
    return true;
}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Integer HSV -> RGB565.
static uint16_t hsv565(uint8_t h, uint8_t s, uint8_t v)
{
    uint8_t region = h / 43;
    uint8_t rem    = (uint8_t)((h - region * 43) * 6);
    uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
    uint8_t q = (uint8_t)((v * (255 - ((s * rem) >> 8))) >> 8);
    uint8_t t = (uint8_t)((v * (255 - ((s * (255 - rem)) >> 8))) >> 8);
    uint8_t r, g, b;
    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return rgb565(r, g, b);
}

uint16_t faceBackground() { return C_GUTTER; }
bool     faceSmooth()     { return true; }   // animates every frame

// A signed hue offset no larger than HUE_STEP, so a tile only ever drifts to
// a nearby colour.
static inline uint8_t nearbyHue(uint8_t from)
{
    return (uint8_t)(from + (random(2 * HUE_STEP + 1) - HUE_STEP));
}

void faceInit()
{
    randomSeed(esp_random());
    for (int i = 0; i < NCELL; i++) {
        tHue[i]   = random(256);        // the field starts varied...
        tHueTo[i] = nearbyHue(tHue[i]); // ...but each tile only drifts locally
        tProg[i]   = random(256);       // stagger, so nothing starts in phase
        tRate[i]   = RATE_MIN + random(RATE_SPAN);
        tPhase[i]  = random(256);
        tPhRate[i] = 2 + random(4);     // its own breathing period
    }
}

// One animation step, scaled by how much time has actually passed.
//
// The rates below are per-frame numbers tuned on the GC9A01 boards, which
// render flat out at roughly 60 fps. The AMOLED runs its smooth faces at
// screenSweepHz() - 8 Hz - so applying them once a frame there makes the
// field drift about seven times too slowly and the mosaic looks static.
// (It used to match by accident: render() ran once per band, so the field was
// stepped eight times a frame, which happened to cancel out.)
//
// `steps` is therefore how many 60 fps frames this frame stood in for, so the
// field moves at the same real-world rate whatever the back end's cadence is.
static void advanceTiles(bool night, int steps)
{
    if (steps < 1) steps = 1;
    if (steps > 16) steps = 16;            // a long stall must not jump the field

    // Ease across rather than switch: the hue remap and the palette lerp are
    // both driven by this, so dusk arrives as a slow fade.
    int target = night ? 255 : 0;
    int mix    = nightMix;
    if      (mix < target) mix = (mix + MIX_STEP * steps > target) ? target : mix + MIX_STEP * steps;
    else if (mix > target) mix = (mix - MIX_STEP * steps < target) ? target : mix - MIX_STEP * steps;
    nightMix = (uint8_t)mix;

    for (int i = 0; i < NCELL; i++) {
        int p = tProg[i] + tRate[i] * steps;
        if (p >= 255) {
            tHue[i]   = tHueTo[i];             // arrive, then pick a new target
            tHueTo[i] = nearbyHue(tHue[i]);
            tProg[i]  = 0;
            tRate[i]  = RATE_MIN + random(RATE_SPAN);   // fresh rate each hop
        } else {
            tProg[i] = (uint8_t)p;
        }
        tPhase[i] = (uint8_t)(tPhase[i] + tPhRate[i] * steps);
    }
}

// ---- 5x5 pixel digits -----------------------------------------------------
// Five bits per row, MSB is the leftmost tile. Glyphs keep their natural
// widths - a 1 is narrow, as it should be. The resulting shift in the time's
// ink centre is corrected by moving the grid, not by padding the font.
static const uint8_t FONT[10][D_H] = {
    {0x0E, 0x11, 0x11, 0x11, 0x0E},   // 0
    {0x04, 0x0C, 0x04, 0x04, 0x0E},   // 1
    {0x1E, 0x01, 0x0E, 0x10, 0x1F},   // 2
    {0x1E, 0x01, 0x0E, 0x01, 0x1E},   // 3
    {0x12, 0x12, 0x1F, 0x02, 0x02},   // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x1E},   // 5
    {0x0E, 0x10, 0x1E, 0x11, 0x0E},   // 6
    {0x1F, 0x02, 0x04, 0x08, 0x08},   // 7
    {0x0E, 0x11, 0x0E, 0x11, 0x0E},   // 8
    {0x0E, 0x11, 0x0F, 0x01, 0x0E},   // 9
};

static void stampDigit(uint8_t *mask, int d, int c0, int r0)
{
    if (d < 0 || d > 9) return;
    for (int r = 0; r < D_H; r++)
        for (int c = 0; c < D_W; c++)
            if (FONT[d][r] & (0x10 >> c))
                mask[(r0 + r) * GRID + (c0 + c)] = 1;
}

static void buildMask(const struct tm &t, uint8_t *mask)
{
    memset(mask, 0, NCELL);
    stampDigit(mask, (t.tm_hour / 10) % 10, COL0,          ROWTOP);
    stampDigit(mask,  t.tm_hour % 10,       COL0 + COLGAP, ROWTOP);
    stampDigit(mask, (t.tm_min  / 10) % 10, COL0,          ROWBOT);
    stampDigit(mask,  t.tm_min  % 10,       COL0 + COLGAP, ROWBOT);
}

// Bounding box, in cell indices, of the tiles the current time actually
// lights. A 1 is narrower than a 0, so this moves with the digits on show.
static void inkCells(const struct tm &t, int &x0, int &x1, int &y0, int &y1)
{
    const int dg[4][3] = {
        { (t.tm_hour / 10) % 10, COL0,          ROWTOP },
        {  t.tm_hour % 10,       COL0 + COLGAP, ROWTOP },
        { (t.tm_min  / 10) % 10, COL0,          ROWBOT },
        {  t.tm_min  % 10,       COL0 + COLGAP, ROWBOT },
    };
    x0 = y0 = 9999; x1 = y1 = -9999;
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < D_H; r++)
            for (int c = 0; c < D_W; c++)
                if (FONT[dg[k][0]][r] & (0x10 >> c)) {
                    int cx = dg[k][1] + c, cy = dg[k][2] + r;
                    if (cx < x0) x0 = cx;
                    if (cx > x1) x1 = cx;
                    if (cy < y0) y0 = cy;
                    if (cy > y1) y1 = cy;
                }
}

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    const bool step = newFrame(t, subSec);

    uint8_t mask[NCELL];
    buildMask(t, mask);

    // Slide the whole grid so the lit tiles sit centred. Eased a pixel at a
    // time so the correction glides in when the digits change rather than
    // snapping - e.g. going from 09:xx to 10:xx, where a 1 appears.
    int ix0, ix1, iy0, iy1;
    inkCells(t, ix0, ix1, iy0, iy1);
    int tx = 120 - (GRID_OFF + (ix0 * CELL + ix1 * CELL + TILE) / 2);
    int ty = 120 - (GRID_OFF + (iy0 * CELL + iy1 * CELL + TILE) / 2);
    if (step) {
        if      (shiftX < tx) shiftX++;
        else if (shiftX > tx) shiftX--;
        if      (shiftY < ty) shiftY++;
        else if (shiftY > ty) shiftY--;
    }

    g.fillScreen(C_GUTTER);

    // Palette interpolated between the day and night ends.
    const int sat  = DAY_SAT + (NIGHT_SAT - DAY_SAT) * nightMix / 255;
    const int base = DAY_VAL + (NIGHT_VAL - DAY_VAL) * nightMix / 255;
    const int amp  = DAY_AMP + (NIGHT_AMP - DAY_AMP) * nightMix / 255;

    const uint16_t fill = timeValid
        ? rgb565(DIGIT_DAY_R + (DIGIT_NGT_R - DIGIT_DAY_R) * nightMix / 255,
                 DIGIT_DAY_G + (DIGIT_NGT_G - DIGIT_DAY_G) * nightMix / 255,
                 DIGIT_DAY_B + (DIGIT_NGT_B - DIGIT_DAY_B) * nightMix / 255)
        : C_DIGIT_NG;

    for (int r = 0; r < GRID; r++) {
        for (int c = 0; c < GRID; c++) {
            int i = r * GRID + c;
            int x = c * CELL + GRID_OFF + shiftX,
                y = r * CELL + GRID_OFF + shiftY;

            if (mask[i]) {
                // Solid, no outline. The gutter already separates the tiles,
                // so an added border just reads as grime on the digits.
                g.fillRect(x, y, TILE, TILE, fill);
                continue;
            }

            // hue slides toward its neighbour; the int8 cast makes the short
            // way round the wheel fall out for free
            int8_t  d = (int8_t)(tHueTo[i] - tHue[i]);
            uint8_t h = (uint8_t)(tHue[i] + ((d * tProg[i]) >> 8));

            // At night the free-walking hue is squeezed into the blue band.
            // The walk itself is untouched, so tiles keep their relative
            // spread - it just plays out across blues instead of the wheel.
            if (nightMix) {
                uint8_t blue = NIGHT_HUE_LO +
                               (uint8_t)(((int)h * (NIGHT_HUE_HI - NIGHT_HUE_LO)) >> 8);
                int8_t  dh   = (int8_t)(blue - h);
                h = (uint8_t)(h + (int)dh * nightMix / 255);
            }

            // brightness breathes on its own slow sine
            int v = base + (int)(amp * sinf(tPhase[i] * 0.024544f));
            if (v < 0)   v = 0;
            if (v > 255) v = 255;

            g.fillRect(x, y, TILE, TILE, hsv565(h, (uint8_t)sat, (uint8_t)v));
        }
    }

    if (step) {
        // How many 60 fps frames this one stood in for. The GC9A01 boards run
        // flat out and land on 1; the AMOLED at 8 Hz lands on about 7.
        uint32_t now = millis();
        int frames = 1;
        if (lastStepMs) {
            uint32_t dt = now - lastStepMs;
            frames = (int)((dt * 60 + 500) / 1000);
            if (frames < 1) frames = 1;
        }
        lastStepMs = now;
        advanceTiles(isNightNow(t), frames);
    }
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_mosaic

// Registration: the one symbol this file exposes.
const FaceVTable FACE_MOSAIC = {
    "mosaic",
    face_mosaic::faceInit,
    face_mosaic::faceBackground,
    face_mosaic::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_mosaic::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_mosaic::faceRender,
};
