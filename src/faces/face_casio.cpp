// ---------------------------------------------------------------------------
//  Face: "casio"  -  Casio-style segmented LCD, angled panels
//
//  Panelled layout in the style of a digital dive watch: interlocking cells
//  with slanted dividers filling the round bezel, a full-width TIME band, and
//  TEMP, DATE, SECONDS, DAY and weather around it. 24-hour time - all digital
//  faces in this project use 24h.
//
//  Three things make it read as a real LCD rather than "digits on a screen":
//    - unlit segments are drawn as faint ghosts, not left blank, so every
//      cell shows the full figure-8 underneath the value;
//    - the cells are angled quadrilaterals with heavy moulded rims, so they
//      tile the circle the way a real LCD bezel does instead of sitting in a
//      grid;
//    - at night the panels switch to an electroluminescent green glow with
//      dark digits, the way a backlit Casio actually looks - the segments
//      stay dark and the *background* lights up.
//
//  The segment renderer is hand-rolled because TFT_eSPI's built-in font 7 is
//  a fixed 48 px and has no concept of an unlit segment.
// ---------------------------------------------------------------------------
#include "../face.h"

// Wrapped in its own namespace so every face can be linked into the same
// binary: each defines faceInit/faceRender/faceBackground/faceSmooth, which
// would otherwise be six duplicate global symbols.
namespace face_casio {

// ---- panel outlines -------------------------------------------------------
// Quadrilaterals, clockwise from the top-left, with slanted dividers.
//
// The rows are sized around BORDER_W: the rim eats 2*BORDER_W from every
// cell, so each row is tall enough that its label and digits still fit in
// the remaining substrate.
struct Pt { int16_t x, y; };

// These deliberately overrun the 240 px square. Anything outside R_MASK is
// painted back to the shell colour at the end of the frame, so the round
// bezel crops the cells instead of the cells having to fit inside it. That
// is what gives the outer cells their curved edges - and it buys a lot of
// room for bigger digits, since only the *content* has to stay inside the
// visible circle, not the panels.
// Top row runs off the top edge and is cropped, the same way the bottom row
// runs off the bottom - so both ends of the face are filled rather than the
// top sitting inside a band of dead case. The divider slope is unchanged.
static const Pt POLY_TEMP[4] = { {  0,   0}, {145,   0}, {130,  72}, {  0,  72} };
static const Pt POLY_DATE[4] = { {153,   0}, {240,   0}, {236,  72}, {138,  72} };
static const Pt POLY_SEC [4] = { { 16,  76}, {240,  76}, {232, 108}, {  4, 108} };
static const Pt POLY_TIME[4] = { {  0, 112}, {240, 112}, {232, 182}, {  4, 182} };
// Bottom row: the divider leans the opposite way to the TEMP/DATE one above,
// so the two splits mirror each other across the face. That makes the
// right-hand cell the roomier of the two, which is why the day name lives
// there and the weather icon - which needs far less width - sits left.
// Divider sits at 108 rather than mid-cell: the icon needs ~30 px and the
// day name ~46, so the split is biased to give the word the room. Measured
// across the rows the content occupies, that leaves 42 px for the icon and
// 59 px for the text. Moving it further left starves the icon.
static const Pt POLY_WX  [4] = { {  0, 186}, {100, 186}, {112, 240}, {  0, 240} };
static const Pt POLY_DAY [4] = { {108, 186}, {240, 186}, {240, 240}, {120, 240} };

#define R_MASK 116         // visible radius; everything beyond is cropped

#define PANEL_R  8         // corner radius of the moulded cells
#define BORDER_W 4         // rim thickness drawn inside each cell edge

// ---- themes ---------------------------------------------------------------
struct Theme {
    uint16_t shell;   // the case, between panels
    uint16_t border;  // moulded rim drawn around every cell
    uint16_t panel;   // LCD substrate
    uint16_t ink;     // lit segments and labels
    uint16_t ghost;   // unlit segments
};
// Daylight: grey LCD, dark digits. The ghost sits close to the panel colour
// so unlit segments read as a faint figure-8, not as a second set of digits.
// The rim is near-black rather than black so it still reads as an outline
// where a cell meets the case.
static const Theme DAY_T   = { 0x0000, 0x2124, 0xAD55, 0x18E3, 0x9CF3 };
// Night: EL backlight. The panel glows, the digits stay dark.
static const Theme NIGHT_T = { 0x0120, 0x12C5, 0x5ED1, 0x0841, 0x4E2E };

#define C_DIRECT 0x07FF   // "running without sprite" marker

static bool nightCache = false;

uint16_t faceBackground() { return nightCache ? NIGHT_T.shell : DAY_T.shell; }
bool     faceSmooth()     { return false; }   // segmented: redraw on the second

// Because the cells are drawn oversized and then cropped, a cell's polygon
// centroid is NOT where it appears on screen - the clipped-away part pulls it
// off. Content placed on the raw polygon drifts toward the crop and can spill
// past the rim into a neighbour. So measure the visible region of each cell
// once at boot and anchor everything to that instead.
struct Box { int16_t cx, cy, x0, y0, x1, y1; };
static Box B_TEMP, B_DATE, B_SEC, B_TIME, B_DAY, B_WX;

static bool pointInPoly(const Pt *p, int n, int x, int y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((p[i].y > y) != (p[j].y > y)) {
            float xc = (float)(p[j].x - p[i].x) * (y - p[i].y) /
                       (float)(p[j].y - p[i].y) + p[i].x;
            if (x < xc) in = !in;
        }
    }
    return in;
}

// Centroid and bounds of (polygon AND visible circle), inset by the rim so
// content never sits on the border.
static void measureCell(const Pt *poly, int n, Box &b)
{
    long sx = 0, sy = 0, cnt = 0;
    int  x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    const int rr = (R_MASK - BORDER_W) * (R_MASK - BORDER_W);

    for (int y = 0; y < 240; y++) {
        int dy = y - 120;
        for (int x = 0; x < 240; x++) {
            int dx = x - 120;
            if (dx * dx + dy * dy > rr)       continue;
            if (!pointInPoly(poly, n, x, y))  continue;
            sx += x; sy += y; cnt++;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    if (!cnt) { b = { 120, 120, 0, 0, 239, 239 }; return; }
    b.cx = sx / cnt; b.cy = sy / cnt;
    b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1;
}

void faceInit()
{
    measureCell(POLY_TEMP, 4, B_TEMP);
    measureCell(POLY_DATE, 4, B_DATE);
    measureCell(POLY_SEC,  4, B_SEC);
    measureCell(POLY_TIME, 4, B_TIME);
    measureCell(POLY_DAY,  4, B_DAY);
    measureCell(POLY_WX,   4, B_WX);
}

// ---- convex polygon inset ------------------------------------------------
// Shift every edge inward along its own normal by d, then intersect each pair
// of consecutive offset edges to get the new corner.
//
// The obvious shortcut - move each vertex a fixed distance toward the
// centroid - is wrong on a skewed quad: it preserves *vertex* distance, not
// *edge* distance, so a sharp corner gets pulled in too little and a shallow
// one too much. That shows up as a rim whose thickness varies around the cell
// and which bulges at the corners, worst on the most slanted panels.
static void insetPoly(const Pt *p, int n, float d, Pt *out)
{
    float cx = 0, cy = 0;
    for (int i = 0; i < n; i++) { cx += p[i].x; cy += p[i].y; }
    cx /= n; cy /= n;

    float ox[8], oy[8], ex[8], ey[8];
    for (int i = 0; i < n && i < 8; i++) {
        int   j  = (i + 1) % n;
        float vx = p[j].x - p[i].x, vy = p[j].y - p[i].y;
        float L  = sqrtf(vx * vx + vy * vy);
        if (L < 0.001f) L = 1.0f;
        vx /= L; vy /= L;

        float nx = -vy, ny = vx;                    // edge normal
        if (nx * (cx - p[i].x) + ny * (cy - p[i].y) < 0) { nx = -nx; ny = -ny; }

        ox[i] = p[i].x + nx * d;                    // a point on the offset edge
        oy[i] = p[i].y + ny * d;
        ex[i] = vx; ey[i] = vy;                     // its direction
    }

    for (int i = 0; i < n && i < 8; i++) {
        int   h     = (i + n - 1) % n;              // the previous edge
        float denom = ex[h] * ey[i] - ey[h] * ex[i];
        if (fabsf(denom) < 0.0001f) {               // parallel: no real corner
            out[i].x = (int16_t)lroundf(ox[i]);
            out[i].y = (int16_t)lroundf(oy[i]);
            continue;
        }
        float t = ((ox[i] - ox[h]) * ey[i] - (oy[i] - oy[h]) * ex[i]) / denom;
        out[i].x = (int16_t)lroundf(ox[h] + ex[h] * t);
        out[i].y = (int16_t)lroundf(oy[h] + ey[h] * t);
    }
}

// ---- rounded convex polygon ----------------------------------------------
// TFT_eSPI has fillRoundRect but nothing for an angled cell. This builds the
// Minkowski sum of the polygon with a disk: inset the outline by r, fill that
// core, then add a disk at each inset vertex and a band along each inset edge
// pushed back out. The result is the original outline with uniform radius-r
// rounded corners and straight edges in between.
template <typename GFX>
static void fillRoundPoly(GFX &g, const Pt *outer, int n, int r, uint16_t c)
{
    Pt in[8];
    insetPoly(outer, n, (float)r, in);

    float cx = 0, cy = 0;
    for (int i = 0; i < n; i++) { cx += in[i].x; cy += in[i].y; }
    cx /= n; cy /= n;

    // core, as a triangle fan
    for (int i = 1; i + 1 < n; i++)
        g.fillTriangle(in[0].x, in[0].y, in[i].x, in[i].y,
                       in[i + 1].x, in[i + 1].y, c);

    // rounded corners
    for (int i = 0; i < n; i++) g.fillCircle(in[i].x, in[i].y, r, c);

    // straight edges, pushed back out along each edge normal
    for (int i = 0; i < n; i++) {
        Pt a = in[i], b = in[(i + 1) % n];
        float dx = b.x - a.x, dy = b.y - a.y;
        float L  = sqrtf(dx * dx + dy * dy);
        if (L < 0.001f) continue;
        float nx = dy / L, ny = -dx / L;
        if (nx * ((a.x + b.x) * 0.5f - cx) + ny * ((a.y + b.y) * 0.5f - cy) < 0) {
            nx = -nx; ny = -ny;
        }
        int ax = a.x + (int)lroundf(nx * r), ay = a.y + (int)lroundf(ny * r);
        int bx = b.x + (int)lroundf(nx * r), by = b.y + (int)lroundf(ny * r);
        g.fillTriangle(a.x, a.y, b.x, b.y, bx, by, c);
        g.fillTriangle(a.x, a.y, bx, by, ax, ay, c);
    }
}

// Draw one cell: the rim is the polygon at full size, then the LCD substrate
// is filled inset by BORDER_W so the rim shows as a heavy outline of even
// thickness. Insetting (rather than growing the rim outward) keeps the cell
// where the layout put it.
template <typename GFX>
static void drawCell(GFX &g, const Pt *poly, int n, const Theme &th)
{
    fillRoundPoly(g, poly, n, PANEL_R, th.border);

    Pt inner[8];
    insetPoly(poly, n, (float)BORDER_W, inner);
    int r = PANEL_R - BORDER_W;
    fillRoundPoly(g, inner, n, r < 2 ? 2 : r, th.panel);
}

// Crop everything outside the visible circle back to the case colour. Two
// spans per scanline, so it costs a couple of memsets per row - cheap, and
// it lets every cell be drawn oversized and simply cut to shape.
template <typename GFX>
static void maskToCircle(GFX &g, int r, uint16_t c)
{
    for (int y = 0; y < 240; y++) {
        int dy = y - 120;
        int d2 = r * r - dy * dy;
        if (d2 <= 0) { g.drawFastHLine(0, y, 240, c); continue; }
        int half = (int)sqrtf((float)d2);
        int x0 = 120 - half, x1 = 120 + half;
        if (x0 > 0)   g.drawFastHLine(0,  y, x0,        c);
        if (x1 < 240) g.drawFastHLine(x1, y, 240 - x1,  c);
    }
}

// ---- 7-segment renderer ---------------------------------------------------
//   bit: a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40
static const uint8_t SEG_MAP[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// Draw one cell from an explicit segment mask.
template <typename GFX>
static void seg7mask(GFX &g, int x, int y, int w, int h, int t, uint8_t m,
                     uint16_t on, uint16_t off)
{
    int half = (h - t) / 2;        // y of the middle bar
    int vh   = (h - 3 * t) / 2;    // length of one vertical segment
    if (vh < 1) vh = 1;

    g.fillRect(x + t,     y,            w - 2 * t, t,  (m & 0x01) ? on : off); // a
    g.fillRect(x,         y + t,        t,         vh, (m & 0x20) ? on : off); // f
    g.fillRect(x + w - t, y + t,        t,         vh, (m & 0x02) ? on : off); // b
    g.fillRect(x + t,     y + half,     w - 2 * t, t,  (m & 0x40) ? on : off); // g
    g.fillRect(x,         y + half + t, t,         vh, (m & 0x10) ? on : off); // e
    g.fillRect(x + w - t, y + half + t, t,         vh, (m & 0x04) ? on : off); // c
    g.fillRect(x + t,     y + h - t,    w - 2 * t, t,  (m & 0x08) ? on : off); // d
}

// d < 0 draws the cell with every segment unlit.
template <typename GFX>
static void seg7(GFX &g, int x, int y, int w, int h, int t, int d,
                 uint16_t on, uint16_t off)
{
    seg7mask(g, x, y, w, h, t,
             (d >= 0 && d <= 9) ? SEG_MAP[d] : 0x00, on, off);
}

// ---- 14-segment (starburst) cells ----------------------------------------
// Seven segments cannot render M or W - both need a centre stroke that simply
// is not there, so any attempt is a fudge. A 14-segment cell adds the two
// centre verticals and the four diagonals, which is what real alphanumeric
// LCDs use, and every letter then renders as itself.
//
//   A =0x0001  B =0x0002  C =0x0004  D=0x0008  E=0x0010  F=0x0020
//   G1=0x0040  G2=0x0080  H =0x0100  I=0x0200  J=0x0400  K=0x0800
//   L =0x1000  M =0x2000
// H/J are the upper diagonals, K/M the lower ones; I/L the centre verticals.
template <typename GFX>
static void seg14(GFX &g, int x, int y, int w, int h, int t, uint16_t m,
                  uint16_t on, uint16_t off, uint16_t bg)
{
    int half = (h - t) / 2;
    int vh   = (h - 3 * t) / 2;  if (vh < 1) vh = 1;
    int cx   = x + (w - t) / 2;              // left edge of the centre column
    int hw   = (w - 3 * t) / 2;  if (hw < 1) hw = 1;   // half of the middle bar

    // horizontals
    g.fillRect(x + t,     y,           w - 2 * t, t, (m & 0x0001) ? on : off); // A
    g.fillRect(x + t,     y + h - t,   w - 2 * t, t, (m & 0x0008) ? on : off); // D
    g.fillRect(x + t,     y + half,    hw,        t, (m & 0x0040) ? on : off); // G1
    g.fillRect(cx + t,    y + half,    hw,        t, (m & 0x0080) ? on : off); // G2
    // outer verticals
    g.fillRect(x,         y + t,        t, vh, (m & 0x0020) ? on : off);       // F
    g.fillRect(x,         y + half + t, t, vh, (m & 0x0010) ? on : off);       // E
    g.fillRect(x + w - t, y + t,        t, vh, (m & 0x0002) ? on : off);       // B
    g.fillRect(x + w - t, y + half + t, t, vh, (m & 0x0004) ? on : off);       // C
    // centre verticals
    g.fillRect(cx,        y + t,        t, vh, (m & 0x0200) ? on : off);       // I
    g.fillRect(cx,        y + half + t, t, vh, (m & 0x1000) ? on : off);       // L

    // diagonals
    float wd = t * 0.9f; if (wd < 1.5f) wd = 1.5f;
    g.drawWideLine(x + t + 1,     y + t + 1,        cx - 1,        y + half - 1,
                   wd, (m & 0x0100) ? on : off, bg);                           // H
    g.drawWideLine(x + w - t - 1, y + t + 1,        cx + t + 1,    y + half - 1,
                   wd, (m & 0x0400) ? on : off, bg);                           // J
    g.drawWideLine(cx - 1,        y + half + t + 1, x + t + 1,     y + h - t - 1,
                   wd, (m & 0x0800) ? on : off, bg);                           // K
    g.drawWideLine(cx + t + 1,    y + half + t + 1, x + w - t - 1, y + h - t - 1,
                   wd, (m & 0x2000) ? on : off, bg);                           // M
}

static uint16_t seg14ForLetter(char c)
{
    switch (c) {
        case 'A': return 0x00F7;
        case 'D': return 0x120F;
        case 'E': return 0x00F9;
        case 'F': return 0x00F1;
        case 'H': return 0x00F6;
        case 'I': return 0x1209;
        case 'M': return 0x0536;   // uprights + both upper diagonals
        case 'N': return 0x2136;
        case 'O': return 0x003F;
        case 'R': return 0x20F3;
        case 'S': return 0x00ED;
        case 'T': return 0x1201;
        case 'U': return 0x003E;
        case 'W': return 0x2836;   // uprights + both lower diagonals
        default:  return 0x0000;
    }
}

// A short word in 14-segment cells, centred on (cx, cy).
template <typename GFX>
static void seg14Word(GFX &g, const char *w, int cx, int cy,
                      int dw, int dh, int dt, int gap,
                      uint16_t on, uint16_t off, uint16_t bg)
{
    int n = (int)strlen(w);
    int x = cx - (n * dw + (n - 1) * gap) / 2;
    int y = cy - dh / 2;
    for (int i = 0; i < n; i++)
        seg14(g, x + i * (dw + gap), y, dw, dh, dt,
              seg14ForLetter(w[i]), on, off, bg);
}

template <typename GFX>
static void seg7Pair(GFX &g, int x, int y, int w, int h, int t, int gap,
                     int value, uint16_t on, uint16_t off, bool valid = true)
{
    int hi = (value / 10) % 10, lo = value % 10;
    seg7(g, x,           y, w, h, t, valid ? hi : -1, on, off);
    seg7(g, x + w + gap, y, w, h, t, valid ? lo : -1, on, off);
}

// caption, left-aligned
template <typename GFX>
static void label(GFX &g, const char *s, int x, int y, const Theme &th, int font)
{
    g.setTextDatum(TL_DATUM);
    g.setTextColor(th.ink, th.panel);
    g.drawString(s, x, y, font);
}

// caption, centred
template <typename GFX>
static void labelC(GFX &g, const char *s, int cx, int y, const Theme &th, int font)
{
    g.setTextDatum(TC_DATUM);
    g.setTextColor(th.ink, th.panel);
    g.drawString(s, cx, y, font);
}

// two digits, centred on cx
template <typename GFX>
static void seg7PairC(GFX &g, int cx, int y, int w, int h, int t, int gap,
                      int v, uint16_t on, uint16_t off)
{
    seg7Pair(g, cx - (2 * w + gap) / 2, y, w, h, t, gap, v, on, off);
}

static const char *DAY_NAMES[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;                       // a segmented display does not sweep
    const bool   night = isNightNow(t);
    const Theme &th    = night ? NIGHT_T : DAY_T;
    nightCache = night;

    g.fillScreen(th.shell);

    drawCell(g, POLY_TEMP, 4, th);
    drawCell(g, POLY_DATE, 4, th);
    drawCell(g, POLY_SEC,  4, th);
    drawCell(g, POLY_TIME, 4, th);
    drawCell(g, POLY_DAY,  4, th);
    drawCell(g, POLY_WX,   4, th);

    // ---- TEMP -------------------------------------------------------------
    // Everything below is anchored to a measured visible centre, never to a
    // raw polygon coordinate: the crop moves each cell's apparent middle.
    {
        char lab[12];
        if (wxValid) strcpy(lab, "TEMP");
        else         snprintf(lab, sizeof(lab), "TEMP E%d", (int)wxErr);
        labelC(g, lab, B_TEMP.cx, B_TEMP.cy - 24, th, 2);

        const int dw = 18, dh = 24, dt = 4, dg = 3;
        int av = wxValid ? abs(wxTempF) : 0;
        int digits[3], n;
        if (av >= 100) { n = 3; digits[0] = av / 100; digits[1] = (av / 10) % 10; digits[2] = av % 10; }
        else           { n = 2; digits[0] = (av / 10) % 10; digits[1] = av % 10; }

        int wsum  = n * dw + (n - 1) * dg;
        int left  = B_TEMP.cx - (wsum + 14) / 2;   // digits + degree ring + F
        int dy    = B_TEMP.cy - 4;

        for (int i = 0; i < n; i++)
            seg7(g, left + i * (dw + dg), dy, dw, dh, dt,
                 wxValid ? digits[i] : -1, th.ink, th.ghost);

        // degree ring + F, because the built-in fonts have no degree glyph
        g.drawCircle(left + wsum + 6, dy + 4, 3, th.ink);
        label(g, "F", left + wsum + 2, dy + 9, th, 2);
    }

    // ---- DATE -------------------------------------------------------------
    labelC(g, "DATE", B_DATE.cx, B_DATE.cy - 26, th, 2);
    seg7PairC(g, B_DATE.cx, B_DATE.cy - 6, 16, 24, 4, 3,
              t.tm_mday, th.ink, th.ghost);

    // ---- SECONDS band -----------------------------------------------------
    // A wide, short band: caption hard left, value hard right.
    label(g, "SECONDS", B_SEC.x0 + 16, B_SEC.cy - 8, th, 2);
    seg7Pair(g, B_SEC.x1 - 46, B_SEC.cy - 9, 14, 18, 3, 2,
             t.tm_sec, th.ink, th.ghost);

    // WiFi/NTP indicator lives in the gap between this band's caption and its
    // digits. It used to sit on the case above the top row, which made that
    // margin read as dead space.
    g.fillSmoothCircle(B_SEC.cx + 6, B_SEC.cy, 3, statusCol, th.panel);
    if (!useSprite)
        g.fillSmoothCircle(B_SEC.cx - 10, B_SEC.cy, 3, C_DIRECT, th.panel);

    // ---- TIME (24 hour) ---------------------------------------------------
    // No caption here: the time is self-evident and the cell reads better
    // with the digits centred in it rather than pushed down by a label.
    {
        const int dw = 40, dh = 46, dt = 7, dg = 5, cw = 7;
        int total = 4 * dw + 4 * dg + cw;
        int x     = B_TIME.cx - total / 2;
        int dy    = B_TIME.cy - dh / 2;

        seg7Pair(g, x, dy, dw, dh, dt, dg, t.tm_hour, th.ink, th.ghost);

        int cx = x + 2 * dw + 2 * dg;
        g.fillRect(cx, dy + 12,      cw, cw, th.ink);
        g.fillRect(cx, dy + dh - 19, cw, cw, th.ink);

        seg7Pair(g, cx + cw + dg, dy, dw, dh, dt, dg, t.tm_min, th.ink, th.ghost);
    }

    // ---- DAY / weather ----------------------------------------------------
    // No caption: three letters in the bottom-right cell are unambiguous, and
    // dropping it lets the day name sit on the cell's visible centre.
    seg14Word(g, DAY_NAMES[t.tm_wday % 7], B_DAY.cx, B_DAY.cy,
              16, 26, 3, 3, th.ink, th.ghost, th.panel);

    wxIconMono(g, B_WX.cx, B_WX.cy, wxValid ? iconForCode(wxCode) : WX_UNKNOWN,
               wxIsDay, th.ink, th.panel);

    maskToCircle(g, R_MASK, th.shell);
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_casio

// Registration: the one symbol this file exposes.
const FaceVTable FACE_CASIO = {
    "casio",
    face_casio::faceInit,
    face_casio::faceBackground,
    face_casio::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_casio::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_casio::faceRender,
};
