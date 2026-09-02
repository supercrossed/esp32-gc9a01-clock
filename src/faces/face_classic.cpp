// ---------------------------------------------------------------------------
//  Face: "classic"  -  ivory dress dial, blued Breguet hands, small seconds
//
//  The traditional analog face. What makes a dial like this read as a real
//  one is the furniture, so all of it is here: a railroad minute track (sixty
//  ticks fenced between two printed circles, with heavier hour marks), upright
//  Roman numerals, a framed date window at 3 where the III would be, a
//  small-seconds sub-dial at 6 where the VI would be, with its own track and
//  numbers, and blued-steel Breguet hands, the ones with the hollow "moon"
//  near the tip. There is no sweeping centre seconds hand; the seconds live
//  in the sub-dial, as they do on a watch with this layout.
//
//  Redrawn every frame for the sweep, so it has to stay cheap: the track is
//  plain lines, anti-aliasing is spent on the hour marks and the hands only.
// ---------------------------------------------------------------------------
#include "../face.h"

namespace face_classic {

#define CX      120
#define CY      120

// palette
#define C_DIAL      0xF79E   // ivory
#define C_BEZEL     0x2104   // thin dark rim at the very edge
#define C_INK       0x0000   // printed track and numerals
#define C_INK_SOFT  0x4A49   // sub-dial numerals
#define C_HAND      0x1152   // blued steel: deep, not bright
#define C_DATE_BG   0xFFFF
#define C_DATE_INK  0x0000

// geometry
#define R_EDGE     119
#define R_TRACK_O  116       // railroad track, outer rail
#define R_TRACK_I  108       // inner rail
#define R_HOUR_I   103       // hour marks reach further in
#define R_NUM       86       // numeral centres
#define DATE_W      26
#define DATE_H      20
#define SUB_CX     120       // small seconds
#define SUB_CY     172
#define SUB_R       24

uint16_t faceBackground() { return C_DIAL; }
bool     faceSmooth()     { return true; }

static const char *ROMAN[12] = {
    "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"
};

struct Tick { int16_t x0, y0, x1, y1; };
static Tick ticks[60];
static Tick subTicks[12];
struct Num  { int16_t x, y; };
static Num  numPos[12];

void faceInit()
{
    for (int i = 0; i < 60; i++) {
        float a = i * 6.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        float rIn = (i % 5 == 0) ? R_HOUR_I : R_TRACK_I;
        ticks[i] = { (int16_t)lroundf(CX + rIn * s),       (int16_t)lroundf(CY - rIn * c),
                     (int16_t)lroundf(CX + R_TRACK_O * s), (int16_t)lroundf(CY - R_TRACK_O * c) };
    }
    for (int i = 0; i < 12; i++) {
        float a = i * 30.0f * DEG_TO_RAD;
        float s = sinf(a), c = cosf(a);
        subTicks[i] = { (int16_t)lroundf(SUB_CX + (SUB_R - 4) * s), (int16_t)lroundf(SUB_CY - (SUB_R - 4) * c),
                        (int16_t)lroundf(SUB_CX + SUB_R * s),       (int16_t)lroundf(SUB_CY - SUB_R * c) };
        numPos[i] = { (int16_t)lroundf(CX + R_NUM * s), (int16_t)lroundf(CY - R_NUM * c) };
    }
}

template <typename GFX>
static void drawDial(GFX &g)
{
    g.drawSmoothCircle(CX, CY, R_EDGE, C_BEZEL, C_DIAL);

    // railroad track: two rails, a tick every minute, hour marks heavier
    g.drawCircle(CX, CY, R_TRACK_O, C_INK);
    g.drawCircle(CX, CY, R_TRACK_I, C_INK);
    for (int i = 0; i < 60; i++) {
        const Tick &t = ticks[i];
        if (i % 5 == 0) g.drawWideLine(t.x0, t.y0, t.x1, t.y1, 2.5f, C_INK, C_DIAL);
        else            g.drawLine(t.x0, t.y0, t.x1, t.y1, C_INK);
    }

    // numerals, upright; the III gives way to the date, the VI to the seconds
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_INK, C_DIAL);
    for (int i = 0; i < 12; i++) {
        int n = i == 0 ? 12 : i;
        if (n == 3 || n == 6) continue;
        g.drawString(ROMAN[n - 1], numPos[i].x, numPos[i].y, 4);
    }
}

template <typename GFX>
static void drawDate(GFX &g, const struct tm &t)
{
    int x = numPos[3].x - DATE_W / 2, y = CY - DATE_H / 2;
    g.fillRect(x, y, DATE_W, DATE_H, C_DATE_BG);
    g.drawRect(x - 1, y - 1, DATE_W + 2, DATE_H + 2, C_DATE_INK);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_DATE_INK, C_DATE_BG);
    g.drawNumber(t.tm_mday, numPos[3].x, CY, 2);
}

template <typename GFX>
static void drawSmallSeconds(GFX &g, float s)
{
    g.drawCircle(SUB_CX, SUB_CY, SUB_R, C_INK);
    for (int i = 0; i < 12; i++) {
        const Tick &t = subTicks[i];
        g.drawLine(t.x0, t.y0, t.x1, t.y1, C_INK);
    }
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_INK_SOFT, C_DIAL);
    g.drawString("60", SUB_CX,      SUB_CY - 13, 1);
    g.drawString("15", SUB_CX + 13, SUB_CY,      1);
    g.drawString("30", SUB_CX,      SUB_CY + 13, 1);
    g.drawString("45", SUB_CX - 13, SUB_CY,      1);

    float a = s * 6.0f * DEG_TO_RAD;
    float sn = sinf(a), cs = cosf(a);
    g.drawWideLine(SUB_CX - 6 * sn, SUB_CY + 6 * cs,
                   SUB_CX + (SUB_R - 4) * sn, SUB_CY - (SUB_R - 4) * cs,
                   1.5f, C_HAND, C_DIAL);
    g.fillSmoothCircle(SUB_CX, SUB_CY, 2, C_HAND, C_DIAL);
}

// Breguet hand: a tapered shaft, a hollow ring near the end, a short point
// beyond it. `ringR` is where the ring sits; `tip` is the overall reach.
template <typename GFX>
static void breguetHand(GFX &g, float ang, float back, float ringR, float tip, float w)
{
    float s = sinf(ang), c = cosf(ang);
    g.drawWideLine(CX - back * s, CY + back * c,
                   CX + (ringR - 5) * s, CY - (ringR - 5) * c, w, C_HAND, C_DIAL);
    int rx = lroundf(CX + ringR * s), ry = lroundf(CY - ringR * c);
    g.fillSmoothCircle(rx, ry, 6, C_HAND, C_DIAL);
    g.fillSmoothCircle(rx, ry, 3, C_DIAL, C_HAND);
    g.drawWideLine(CX + (ringR + 5) * s, CY - (ringR + 5) * c,
                   CX + tip * s, CY - tip * c, w * 0.6f, C_HAND, C_DIAL);
}

template <typename GFX>
static void drawHands(GFX &g, float h, float m)
{
    breguetHand(g, h * 30.0f * DEG_TO_RAD, 10, 56,  72, 5.0f);
    breguetHand(g, m *  6.0f * DEG_TO_RAD, 12, 84, 104, 3.5f);
    // hub, with the WiFi/NTP state as a small dot in its centre
    g.fillSmoothCircle(CX, CY, 5, C_HAND, C_DIAL);
    g.fillSmoothCircle(CX, CY, 2, statusCol, C_HAND);
}

template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    float s = t.tm_sec + subSec;
    float m = t.tm_min + s / 60.0f;
    float h = (t.tm_hour % 12) + m / 60.0f;

    g.fillScreen(C_DIAL);
    drawDial(g);
    drawDate(g, t);
    drawSmallSeconds(g, s);
    drawHands(g, h, m);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

// Between seconds only the small-seconds sub-dial changes.
static int faceDirty(const struct tm &, float, const struct tm &, float, DirtyRect *out, int max)
{
    if (max < 1) return 0;
    out[0] = { SUB_CX - SUB_R - 3, SUB_CY - SUB_R - 3, 2 * SUB_R + 6, 2 * SUB_R + 6 };
    return 1;
}

} // namespace face_classic

const FaceVTable FACE_CLASSIC = {
    "classic",
    face_classic::faceInit,
    face_classic::faceBackground,
    face_classic::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_classic::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_classic::faceRender,
    face_classic::faceDirty,
};
