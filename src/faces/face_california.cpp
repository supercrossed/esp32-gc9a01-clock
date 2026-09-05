// ---------------------------------------------------------------------------
//  Face: "california"  -  cream numerals on deep blue, half Roman half Arabic
//
//  The California dial: Roman numerals across the top half, Arabic across the
//  bottom, batons at the quarters. It is an old watchmaking layout and reads
//  as a dress watch without being a plain one - the mix is the whole point of
//  it, so both alphabets are drawn at the same weight and size.
//
//  What sits where, going clockwise from the top:
//
//     12   an inverted triangle, not a numeral
//     I II   Roman
//     3    a baton
//     4 5   Arabic
//     6    a baton
//     7 8   Arabic
//     9    a baton
//     X XI   Roman
//
//  A minute track of fine ticks runs round the rim, heavier at the hours. The
//  date sits under 12, and where a moonphase would go at 6 there is instead
//  the current weather - this clock knows the sky, and a moon it cannot see
//  is less use than the rain it can.
//
//  Cream hands with a hairline red seconds hand, as on the original.
// ---------------------------------------------------------------------------
#include "../face.h"

namespace face_california {

#define CX      120
#define CY      120

// ---- palette ---------------------------------------------------------------
// A deep navy, not black: the numerals have to sit ON something for the dial
// to read as enamel rather than as a hole.
// Sampled off the reference rather than guessed: the dial is a lighter,
// slightly steely blue than a true navy, and the printing is bone rather
// than white - at pure white the numerals glare and stop looking painted.
#define C_DIAL      0x114A   // indigo: deeper than a steel blue, and with
                             // more blue against the green so it reads as
                             // ink rather than sky
#define C_INK       0xCE35   // A warm bone. The sampled #D8CFC0 still read as
                             // white on the AMOLED, which is brighter and
                             // more saturated than the reference photo - so
                             // this is a step warmer and a step down, which
                             // is what makes it look like paint rather than
                             // backlight. Still 8.5:1 on the dial.
#define C_INK_DIM   0x6B4D   // the minute track between hours, warmed to match
#define C_SEC       0xF986   // the seconds hairline
#define C_DATE      0xCE35
#define C_EDGE      0x0000   // the hairline the hands are outlined in
#define C_SUB       0x1189   // the well the weather sits in, a shade darker

// ---- geometry --------------------------------------------------------------
#define R_TICK_O   116       // outer end of the minute track
#define R_TICK_I   108       // inner end, ordinary minutes
#define R_TICK_H   103       // inner end at the hours
#define R_NUM       86       // numeral centres
// The well sits between the hub and the 6 baton, clear of both: at radius 22
// centred on 158 it swallowed the baton entirely.
#define SUB_CY     152       // weather well, below the hub
#define SUB_R       19

uint16_t faceBackground() { return C_DIAL; }
bool     faceSmooth()     { return true; }

// What is printed at each hour. 12 is the triangle and 3/6/9 are batons, so
// those are drawn rather than set here.
static const char *MARKS[12] = {
    nullptr, "I", "II", nullptr, "4", "5",
    nullptr, "7", "8", nullptr, "X", "XI"
};

struct Pt { int16_t x, y; };
static Pt numPos[12];

void faceInit()
{
    for (int i = 0; i < 12; i++) {
        float a = i * 30.0f * DEG_TO_RAD;
        numPos[i].x = (int16_t)lroundf(CX + R_NUM * sinf(a));
        numPos[i].y = (int16_t)lroundf(CY - R_NUM * cosf(a));
    }
}

// ---- the dial --------------------------------------------------------------
template <typename GFX>
static void drawDial(GFX &g)
{
    // minute track: a fine tick a minute, longer and brighter on the hours
    for (int i = 0; i < 60; i++) {
        float a = i * 6.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        bool  hour = (i % 5 == 0);
        int   ri   = hour ? R_TICK_H : R_TICK_I;
        float x0 = CX + ri * s,       y0 = CY - ri * c;
        float x1 = CX + R_TICK_O * s, y1 = CY - R_TICK_O * c;
        // Both drawn as wide lines. drawLine() would be a single logical
        // pixel, which on this panel is thin enough that its anti-aliasing
        // has almost nothing to work with and the track reads as stepped.
        if (hour) g.drawWideLine(x0, y0, x1, y1, 3.5f, C_INK,     C_DIAL);
        else      g.drawWideLine(x0, y0, x1, y1, 1.6f, C_INK_DIM, C_DIAL);
    }

    // the numerals, and the batons that stand in for 3, 6 and 9
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_INK, C_DIAL);
    for (int i = 0; i < 12; i++) {
        if (MARKS[i]) {
            // Font 8: FreeSans at the panel's own resolution, rather than a
            // 26 px bitmap doubled. The numerals are the whole character of
            // this dial, so they are the thing worth spending the flash on.
            g.drawString(MARKS[i], numPos[i].x, numPos[i].y, 9);
            continue;
        }
        if (i == 0) continue;                     // 12 is the triangle, below
        // A baton at 3, 6 and 9, lying along the radius.
        float a = i * 30.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        g.drawWideLine(CX + (R_NUM - 9) * s, CY - (R_NUM - 9) * c,
                       CX + (R_NUM + 9) * s, CY - (R_NUM + 9) * c,
                       5.0f, C_INK, C_DIAL);
    }

    // 12 o'clock: a filled triangle pointing down into the dial, with its
    // corners rounded off. A hard-pointed triangle is the one sharp thing on
    // a dial whose every other element is a curve or a round-capped stroke.
    //
    // Drawn as a smaller triangle with its three edges stroked: a wide line
    // already has round caps, so stroking the inset shape rounds each corner
    // exactly and brings the outline back out to full size. The inset is
    // each vertex pulled toward the centroid by the corner radius.
    const int ty = CY - R_NUM;
    const float RR = 3.0f;
    const float vx[3] = { CX - 8.4f, CX + 8.4f, (float)CX };
    const float vy[3] = { ty - 8.4f, ty - 8.4f, ty + 7.0f };
    g.fillTriangle((int)vx[0], (int)vy[0], (int)vx[1], (int)vy[1],
                   (int)vx[2], (int)vy[2], C_INK);
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        g.drawWideLine(vx[i], vy[i], vx[j], vy[j], 2.0f * RR, C_INK, C_DIAL);
    }
}

// The date, printed under 12 the way the original carries it.
template <typename GFX>
static void drawDate(GFX &g, const struct tm &t)
{
    static const char *WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    char buf[12];
    snprintf(buf, sizeof buf, "%s %d", WD[t.tm_wday % 7], t.tm_mday);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_DATE, C_DIAL);
    g.drawString(buf, CX, CY - 44, 2);
}

// Where the original puts a moonphase, this puts the sky it can actually
// see. No well or ring around it: on a printed dial an aperture would have a
// bezel, but this is an icon sitting on the enamel, and a circle round it
// just competed with the sub-dial-free look of the rest of the face.
template <typename GFX>
static void drawWeather(GFX &g)
{
    wxIconColor(g, CX, SUB_CY, wxValid ? iconForCode(wxCode) : WX_UNKNOWN,
                wxIsDay, C_DIAL);
}

// ---- hands -----------------------------------------------------------------
// Baton hands with a squared end, cream on blue. The seconds hand is a plain
// hairline in red with a long counterweight, which is what the original has -
// no taper, no lume.
template <typename GFX>
static void drawHands(GFX &g, float h, float m, float s)
{
    float ah = h * 30.0f * DEG_TO_RAD;
    float am = m *  6.0f * DEG_TO_RAD;
    float as = s *  6.0f * DEG_TO_RAD;

    // Each hand is drawn twice: a slightly wider dark line first, then the
    // cream one on top of it. That leaves a hairline of outline showing all
    // round, which is what stops a pale hand disappearing as it crosses a
    // numeral - the reference has the same edging.
    g.drawWideLine(CX - 14 * sinf(ah), CY + 14 * cosf(ah),
                   CX + 58 * sinf(ah), CY - 58 * cosf(ah), 9.0f, C_EDGE, C_DIAL);
    g.drawWideLine(CX - 14 * sinf(ah), CY + 14 * cosf(ah),
                   CX + 58 * sinf(ah), CY - 58 * cosf(ah), 7.0f, C_INK, C_EDGE);
    g.drawWideLine(CX - 18 * sinf(am), CY + 18 * cosf(am),
                   CX + 92 * sinf(am), CY - 92 * cosf(am), 7.0f, C_EDGE, C_DIAL);
    g.drawWideLine(CX - 18 * sinf(am), CY + 18 * cosf(am),
                   CX + 92 * sinf(am), CY - 92 * cosf(am), 5.0f, C_INK, C_EDGE);
    g.drawWideLine(CX - 30 * sinf(as), CY + 30 * cosf(as),
                   CX + 100 * sinf(as), CY - 100 * cosf(as), 1.5f, C_SEC, C_DIAL);

    // the hub: cream boss, red centre to match the seconds hand
    g.fillSmoothCircle(CX, CY, 5, C_INK, C_DIAL);
    g.fillSmoothCircle(CX, CY, 2, C_SEC, C_INK);
}

// ---- the face --------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    float s = t.tm_sec + subSec;
    float m = t.tm_min + s / 60.0f;
    float h = (t.tm_hour % 12) + m / 60.0f;

    g.fillScreen(C_DIAL);
    drawDial(g);
    drawDate(g, t);
    drawWeather(g);
    drawHands(g, h, m, s);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

// The seconds hand sweeps, so its old and new positions are what change
// between frames; the date and the weather change on the minute at most and
// are covered by the rolling background band.
static int faceDirty(const struct tm &t, float sub, const struct tm &pt, float psub,
                     DirtyRect *out, int max)
{
    int n = handBoxes(CX, CY, 30, 100, secAngle(pt, psub), 7, 6, out, max);
    n += handBoxes(CX, CY, 30, 100, secAngle(t, sub), 7, 6, out + n, max - n);
    return n;
}

} // namespace face_california

const FaceVTable FACE_CALIFORNIA = {
    "california",
    face_california::faceInit,
    face_california::faceBackground,
    face_california::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_california::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_california::faceRender,
    face_california::faceDirty,
};
