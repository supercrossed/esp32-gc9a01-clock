// ---------------------------------------------------------------------------
//  Face: "orbit"  -  three concentric arcs, no hands
//
//  Time as progress rather than position. Three rings, each one filling as
//  its unit runs out: the outer sweeps once a minute, the middle once an
//  hour, the inner once every twelve. A marker rides the head of each arc so
//  the exact position is readable at a glance, and the digital time sits in
//  the middle for when you want the number rather than the shape.
//
//  It reads differently from a dial: you see how much of the hour is left,
//  not where the hand is pointing. Half past is half a ring, and the ring
//  emptying and refilling is the whole animation.
//
//  The seconds ring sweeps smoothly rather than stepping, which is what the
//  sub-second the back end passes in is for. Everything else moves on the
//  minute, so the dirty box is a band around the outer ring and the middle
//  of the dial.
//
//  Dark ground, cool accents, thin strokes: the modern end of the collection,
//  where casio and pulsar hold the retro end.
// ---------------------------------------------------------------------------
#include "../face.h"

namespace face_orbit {

#define CX      120
#define CY      120

// ---- palette ---------------------------------------------------------------
#define C_BG      0x0862   // near-black, faintly blue
#define C_TRACK   0x1905   // the unfilled part of a ring
#define C_HOUR    0x5E5F   // inner ring: cyan
#define C_MIN     0x7F99   // middle ring: mint
#define C_SEC     0xFACF   // outer ring: coral
#define C_TEXT    0xD6FC
#define C_DIM     0x6BD0
#define C_ACCENT  0xFDEA

// ---- geometry --------------------------------------------------------------
#define R_SEC     108
#define R_MIN      92
#define R_HOUR     76
#define ARC_W       8.0f   // a band, not a hairline

uint16_t faceBackground() { return C_BG; }
bool     faceSmooth()     { return true; }    // the seconds ring sweeps
void     faceInit()       { }

// An arc from 12 o'clock clockwise through `frac` of a turn, drawn as a run
// of short wide lines - the canvas has no arc primitive, and stepping one is
// a better trade than adding a rasteriser for this alone.
//
// The step is derived from the radius rather than fixed. Each segment is a
// round-capped line, so consecutive ones only merge into a continuous stroke
// while the chord between them is shorter than the stroke is wide; past that
// the caps separate and the arc comes out as a string of beads. Chord is
// 2 r sin(step/2), so the step has to shrink as the ring grows.
template <typename GFX>
static void arc(GFX &g, int r, float frac, float weight, uint16_t col, uint16_t bg)
{
    if (frac <= 0.0f) return;
    if (frac > 1.0f) frac = 1.0f;

    // Aim for a chord of about half the stroke width, which overlaps enough
    // to hide the joins without drawing far more segments than are needed.
    float step = 2.0f * asinf((weight * 0.5f) / (2.0f * r)) / DEG_TO_RAD;
    if (step > 4.0f) step = 4.0f;
    if (step < 0.6f) step = 0.6f;

    const float total = frac * 360.0f;
    float prevx = CX, prevy = (float)(CY - r);

    for (float a = step; a <= total + 0.001f; a += step) {
        float aa = (a > total ? total : a) * DEG_TO_RAD;
        float x = CX + r * sinf(aa), y = CY - r * cosf(aa);
        g.drawWideLine(prevx, prevy, x, y, weight, col, bg);
        prevx = x; prevy = y;
    }
}

// The marker at the head of an arc: a filled dot, brighter than the arc, so
// the exact position reads even where the arc is nearly a full turn.
template <typename GFX>
static void head(GFX &g, int r, float frac, uint16_t col)
{
    float a = frac * 360.0f * DEG_TO_RAD;
    int x = (int)lroundf(CX + r * sinf(a));
    int y = (int)lroundf(CY - r * cosf(a));
    g.fillSmoothCircle(x, y, 7, col, C_BG);
    g.fillSmoothCircle(x, y, 3, C_TEXT, col);
}

// ---- the face --------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    g.fillScreen(C_BG);

    const float sec  = t.tm_sec + subSec;
    const float mins = t.tm_min + sec / 60.0f;
    const float hrs  = (t.tm_hour % 12) + mins / 60.0f;

    // The tracks first, as full faint rings, so an arc reads as a filled
    // portion of something rather than a floating line.
    arc(g, R_SEC,  1.0f, ARC_W, C_TRACK, C_BG);
    arc(g, R_MIN,  1.0f, ARC_W, C_TRACK, C_BG);
    arc(g, R_HOUR, 1.0f, ARC_W, C_TRACK, C_BG);

    arc(g, R_HOUR, hrs  / 12.0f, ARC_W, C_HOUR, C_BG);
    arc(g, R_MIN,  mins / 60.0f, ARC_W, C_MIN,  C_BG);
    arc(g, R_SEC,  sec  / 60.0f, ARC_W, C_SEC,  C_BG);

    head(g, R_HOUR, hrs  / 12.0f, C_HOUR);
    head(g, R_MIN,  mins / 60.0f, C_MIN);
    head(g, R_SEC,  sec  / 60.0f, C_SEC);

    // Twelve faint ticks inside the hour ring, so the shape can be read as a
    // time and not only as a proportion.
    for (int i = 0; i < 12; i++) {
        float a = i * 30.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        g.drawWideLine(CX + (R_HOUR - 11) * s, CY - (R_HOUR - 11) * c,
                       CX + (R_HOUR - 6)  * s, CY - (R_HOUR - 6)  * c,
                       i % 3 == 0 ? 2.5f : 1.4f, C_DIM, C_BG);
    }

    // The time in the middle, 24 hour like every digital face here.
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_TEXT, C_BG);
    // Font 9 is the AMOLED's native bold; the GC9A01 boards do not have it.
#ifdef AMOLED_C6
    g.drawString(buf, CX, CY - 8, 9);
#else
    g.drawString(buf, CX, CY - 8, 4);
#endif

    // Date under it, and the weather under that, both small.
    static const char *WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    snprintf(buf, sizeof buf, "%s %d", WD[t.tm_wday % 7], t.tm_mday);
    g.setTextColor(C_DIM, C_BG);
    g.drawString(buf, CX, CY + 16, 2);

    if (wxValid) {
        snprintf(buf, sizeof buf, "%d%c%s", wxTempF, 0xB0, wxUnit());
        g.setTextColor(C_ACCENT, C_BG);
        g.drawString(buf, CX, CY + 36, 2);
    }

    // Link and sync state, as a dot at the foot of the dial.
    g.fillSmoothCircle(CX, CY + 56, 3, statusCol, C_BG);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

// Between two frames the only thing that moves is the head of the seconds
// arc: the part already drawn stays drawn, and the other two rings creep far
// too slowly to matter. So the dirty region is a box around the head's old
// and new positions, the same idea as the hand boxes the analog faces use.
//
// Four bands around the rim was the obvious first answer and it was wrong:
// a ring is not the edge of its bounding square, so at 30, 45 and 60 degrees
// the arc runs through the corners the bands leave out, and those parts of
// the sweep were never repainted.
static int faceDirty(const struct tm &t, float sub, const struct tm &pt, float psub,
                     DirtyRect *out, int max)
{
    // On the minute the arc snaps back to nothing and both other rings step,
    // so the whole dial has to be redrawn.
    if (t.tm_min != pt.tm_min) return 0;

    const float a0 = (pt.tm_sec + psub) * 6.0f * DEG_TO_RAD;
    const float a1 = (t.tm_sec  + sub)  * 6.0f * DEG_TO_RAD;

    // The head marker is r=7, the arc half-width 4; 12 covers both plus the
    // anti-aliased edge.
    const int PAD = 12;
    float x0 = CX + R_SEC * sinf(a0), y0 = CY - R_SEC * cosf(a0);
    float x1 = CX + R_SEC * sinf(a1), y1 = CY - R_SEC * cosf(a1);

    int bx = (int)floorf(fminf(x0, x1)) - PAD;
    int by = (int)floorf(fminf(y0, y1)) - PAD;
    int ex = (int)ceilf (fmaxf(x0, x1)) + PAD;
    int ey = (int)ceilf (fmaxf(y0, y1)) + PAD;

    int n = 0;
    if (n < max) out[n++] = { (int16_t)bx, (int16_t)by,
                              (int16_t)(ex - bx), (int16_t)(ey - by) };
    return n;
}

} // namespace face_orbit

const FaceVTable FACE_ORBIT = {
    "orbit",
    face_orbit::faceInit,
    face_orbit::faceBackground,
    face_orbit::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_orbit::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_orbit::faceRender,
    face_orbit::faceDirty,
};
