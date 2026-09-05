#include "canvas.h"
#include "fonts.h"

// ---- window ---------------------------------------------------------------
void Canvas::attach(uint16_t *b, int x, int y, int ww, int hh)
{
    buf = b; ox = x; oy = y; w = ww; h = hh;
}

inline void Canvas::blend(int32_t x, int32_t y, uint16_t c, int32_t a)
{
    x -= ox; y -= oy;
    if ((uint32_t)x >= (uint32_t)w || (uint32_t)y >= (uint32_t)h) return;
    if (a <= 0) return;
    uint16_t *p = buf + y * w + x;
    if (a >= 256) { *p = c; return; }
    uint16_t d = *p;
    int32_t dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
    int32_t cr = (c >> 11) & 31, cg = (c >> 5) & 63, cb = c & 31;
    dr += ((cr - dr) * a) >> 8;
    dg += ((cg - dg) * a) >> 8;
    db += ((cb - db) * a) >> 8;
    *p = (uint16_t)((dr << 11) | (dg << 5) | db);
}

void Canvas::fillScreen(uint16_t c)
{
    if (!buf) return;
    uint32_t n = (uint32_t)w * h;
    for (uint32_t i = 0; i < n; i++) buf[i] = c;
}

// ---- physical rasterisers -------------------------------------------------
void Canvas::pRect(int32_t x, int32_t y, int32_t rw, int32_t rh, uint16_t c)
{
    int32_t x0 = x - ox, y0 = y - oy, x1 = x0 + rw, y1 = y0 + rh;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int32_t yy = y0; yy < y1; yy++) {
        uint16_t *p = buf + yy * w + x0;
        for (int32_t xx = x0; xx < x1; xx++) *p++ = c;
    }
}

// Disc of radius rq quarter-pixels, edge anti-aliased over one pixel.
// Coverage comes from the squared distance: inside (rq-2)^2 is solid, outside
// (rq+2)^2 is empty, and it ramps linearly in d^2 between - close enough to
// the true ramp in d to be indistinguishable, and there is no sqrt.
void Canvas::pDisc(int32_t cx, int32_t cy, int32_t rq, uint16_t c)
{
    if (rq <= 0) return;
    int32_t rr = (rq + 2) / 4 + 1;
    int32_t y0 = cy - rr, y1 = cy + rr, x0 = cx - rr, x1 = cx + rr;
    if (y0 < oy) y0 = oy;
    if (y1 > oy + h - 1) y1 = oy + h - 1;
    if (x0 < ox) x0 = ox;
    if (x1 > ox + w - 1) x1 = ox + w - 1;
    if (y0 > y1 || x0 > x1) return;
    int64_t outer = (int64_t)(rq + 2) * (rq + 2);
    int64_t inner = (rq > 2) ? (int64_t)(rq - 2) * (rq - 2) : 0;
    int64_t span  = outer - inner;
    for (int32_t y = y0; y <= y1; y++) {
        int32_t dy = y - cy;
        for (int32_t x = x0; x <= x1; x++) {
            int32_t dx = x - cx;
            int64_t d2 = 16LL * (dx * dx + dy * dy);
            if (d2 >= outer) continue;
            if (d2 <= inner) { put(x, y, c); continue; }
            blend(x, y, c, (int32_t)((outer - d2) * 256 / span));
        }
    }
}

// Ring at radius rq, thickness tq (quarter pixels), anti-aliased both edges.
// |d - r| is approximated by |d^2 - r^2| / (2r).
void Canvas::pRing(int32_t cx, int32_t cy, int32_t rq, int32_t tq, uint16_t c, uint8_t quadrants)
{
    if (rq <= 0) return;
    int32_t rr = (rq + tq / 2 + 2) / 4 + 1;
    int32_t y0 = cy - rr, y1 = cy + rr, x0 = cx - rr, x1 = cx + rr;
    if (y0 < oy) y0 = oy;
    if (y1 > oy + h - 1) y1 = oy + h - 1;
    if (x0 < ox) x0 = ox;
    if (x1 > ox + w - 1) x1 = ox + w - 1;
    if (y0 > y1 || x0 > x1) return;

    // The clip above is a bounding-box test, and a ring's bounding box is the
    // whole disc. A big ring - the rim circles are r=231 on a 466 px panel -
    // therefore overlaps almost every window, including ones the stroke does
    // not cross at all, and the loop below then walks every pixel of them
    // doing squared-distance work for nothing. Reject those windows here by
    // asking whether the annulus actually reaches the window: if the nearest
    // corner is outside the ring, or the farthest corner inside it, no pixel
    // in this window can land on the stroke.
    //
    // The test is written in terms of d2 and r2 exactly as the loop is, rather
    // than in true distance: the loop's coverage comes from the linearised
    // |d2 - r2| / (2 rq), which is not the same surface as |d - r|, and a
    // bound derived from real distance rejects windows the loop would in fact
    // have painted.
    {
        int64_t r2sq  = (int64_t)rq * rq;
        int64_t band  = (int64_t)(tq / 2 + 2) * (2 * rq);   // |d2 - r2| the loop accepts
        int32_t ndx = cx < x0 ? x0 - cx : (cx > x1 ? cx - x1 : 0);
        int32_t ndy = cy < y0 ? y0 - cy : (cy > y1 ? cy - y1 : 0);
        int32_t fdx = (cx - x0 > x1 - cx) ? cx - x0 : x1 - cx;
        int32_t fdy = (cy - y0 > y1 - cy) ? cy - y0 : y1 - cy;
        int64_t near2 = 16LL * ((int64_t)ndx * ndx + (int64_t)ndy * ndy);
        int64_t far2  = 16LL * ((int64_t)fdx * fdx + (int64_t)fdy * fdy);
        if (near2 - r2sq > band) return;    // whole window outside the stroke
        if (r2sq - far2  > band) return;    // whole window inside it
    }

    int64_t r2   = (int64_t)rq * rq;
    int32_t half = tq / 2 + 2;                   // quarter px, to the zero-coverage contour
    for (int32_t y = y0; y <= y1; y++) {
        int32_t dy = y - cy;
        for (int32_t x = x0; x <= x1; x++) {
            int32_t dx = x - cx;
            if (quadrants != 0x0F) {
                // bit0: top-left, bit1: top-right, bit2: bottom-left, bit3: bottom-right
                uint8_t q = (dy < 0 ? 0 : 2) | (dx < 0 ? 0 : 1);
                if (!(quadrants & (1 << q))) continue;
            }
            int64_t d2   = 16LL * (dx * dx + dy * dy);
            int64_t diff = d2 - r2; if (diff < 0) diff = -diff;
            int32_t dist = (int32_t)(diff / (2 * rq));   // ~ |d - r| in quarter px
            int32_t cov  = half - dist;                   // quarter px inside the edge
            if (cov <= 0) continue;
            if (cov >= 4) { put(x, y, c); continue; }
            blend(x, y, c, cov * 64);
        }
    }
}

// Anti-aliased line with round caps. Endpoints in 1/16 px, width in 1/4 px.
void Canvas::pWideLine(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t wq, uint16_t c)
{
    int32_t hq = wq / 2;                                   // half width, quarter px
    if (hq < 1) hq = 1;
    int32_t pad = (hq + 2) / 4 + 2;
    int32_t x0 = (min(ax, bx) >> 4) - pad, x1 = (max(ax, bx) >> 4) + pad;
    int32_t y0 = (min(ay, by) >> 4) - pad, y1 = (max(ay, by) >> 4) + pad;
    if (y0 < oy) y0 = oy;
    if (y1 > oy + h - 1) y1 = oy + h - 1;
    if (x0 < ox) x0 = ox;
    if (x1 > ox + w - 1) x1 = ox + w - 1;
    if (y0 > y1 || x0 > x1) return;

    int64_t dx = bx - ax, dy = by - ay;
    int64_t l2 = dx * dx + dy * dy;
    int64_t outer = (int64_t)(hq + 2) * (hq + 2);          // quarter px^2
    int64_t inner = (hq > 2) ? (int64_t)(hq - 2) * (hq - 2) : 0;
    int64_t span  = outer - inner;

    // Distance to the segment, without locating the closest point.
    //
    // The obvious form projects the pixel onto the line - t = dot / l2 - and
    // then measures to that point. That is a 64-bit divide for every pixel of
    // the bounding box, and this chip has no 64-bit divide instruction, so
    // each one is a libgcc call of a hundred cycles or more. On a face that
    // draws three hands and sixty ticks it dominated the whole render.
    //
    // Between the endpoints the perpendicular distance falls straight out of
    // the cross product: |cross| / |ab|, so d2 = cross^2 / l2. Comparing
    // cross^2 against outer * l2 instead removes the division from the test
    // altogether, leaving multiplies. Beyond either endpoint the distance is
    // simply to that endpoint, which needs no division either. Only a pixel
    // actually on the anti-aliased edge divides, and those are a thin
    // fraction of the box.
    //
    // It is also the more accurate form: rounding t into 16.16 and then
    // squaring the error, as the projection did, was losing far more
    // precision than this does.
    const int64_t outerL2 = outer * (l2 ? l2 : 1);
    const int64_t innerL2 = inner * (l2 ? l2 : 1);

    for (int32_t y = y0; y <= y1; y++) {
        int64_t py = (int64_t)y * 16 + 8;                  // pixel centre, 1/16 px
        for (int32_t x = x0; x <= x1; x++) {
            int64_t px = (int64_t)x * 16 + 8;
            int64_t d2;                                    // quarter px^2

            if (l2 == 0) {
                int64_t ex = px - ax, ey = py - ay;
                d2 = (ex * ex + ey * ey) / 16;
                if (d2 >= outer) continue;
                if (d2 <= inner) { put(x, y, c); continue; }
            } else {
                int64_t dot = (px - ax) * dx + (py - ay) * dy;
                if (dot <= 0 || dot >= l2) {               // past an end: a cap
                    int64_t ex = (dot <= 0) ? px - ax : px - bx;
                    int64_t ey = (dot <= 0) ? py - ay : py - by;
                    d2 = (ex * ex + ey * ey) / 16;
                    if (d2 >= outer) continue;
                    if (d2 <= inner) { put(x, y, c); continue; }
                } else {                                    // alongside: the shaft
                    int64_t cross = (px - ax) * dy - (py - ay) * dx;
                    int64_t c2    = cross * cross / 16;     // scale as above
                    if (c2 >= outerL2) continue;
                    if (c2 <= innerL2) { put(x, y, c); continue; }
                    d2 = c2 / l2;                           // only edge pixels divide
                }
            }
            blend(x, y, c, (int32_t)((outer - d2) * 256 / span));
        }
    }
}

void Canvas::pTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       int32_t x2, int32_t y2, uint16_t c)
{
    int32_t minx = min(x0, min(x1, x2)), maxx = max(x0, max(x1, x2));
    int32_t miny = min(y0, min(y1, y2)), maxy = max(y0, max(y1, y2));
    if (minx < ox) minx = ox;
    if (maxx > ox + w - 1) maxx = ox + w - 1;
    if (miny < oy) miny = oy;
    if (maxy > oy + h - 1) maxy = oy + h - 1;
    // The three edge functions are signed areas, so each one is proportional
    // to the distance from that edge - scaled by the edge's length. Dividing
    // by that length turns it into a real distance, which is what lets the
    // border pixels be blended instead of hard-cut. Without it a filled
    // triangle is the one shape on a dial that still has jagged edges, which
    // is conspicuous next to anti-aliased hands and circles.
    //
    // The lengths are per-triangle, so the roots are three per call rather
    // than per pixel.
    const float l1 = sqrtf((float)((y0 - y1) * (y0 - y1) + (x0 - x1) * (x0 - x1)));
    const float l2 = sqrtf((float)((y1 - y2) * (y1 - y2) + (x1 - x2) * (x1 - x2)));
    const float l3 = sqrtf((float)((y2 - y0) * (y2 - y0) + (x2 - x0) * (x2 - x0)));
    const float i1 = l1 > 0 ? 1.0f / l1 : 0.0f;
    const float i2 = l2 > 0 ? 1.0f / l2 : 0.0f;
    const float i3 = l3 > 0 ? 1.0f / l3 : 0.0f;

    // Which way round the vertices were given decides the sign of "inside".
    const int32_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    const float   sgn  = (area < 0) ? -1.0f : 1.0f;

    for (int32_t y = miny; y <= maxy; y++)
        for (int32_t x = minx; x <= maxx; x++) {
            int32_t d1 = (x - x1) * (y0 - y1) - (x0 - x1) * (y - y1);
            int32_t d2 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2);
            int32_t d3 = (x - x0) * (y2 - y0) - (x2 - x0) * (y - y0);

            // Distance to the nearest edge, positive inside.
            float e1 = sgn * d1 * i1, e2 = sgn * d2 * i2, e3 = sgn * d3 * i3;
            float e  = e1 < e2 ? e1 : e2;
            if (e3 < e) e = e3;

            if (e >= 0.5f) { put(x, y, c); continue; }   // well inside
            if (e <= -0.5f) continue;                    // well outside
            blend(x, y, c, (int32_t)((e + 0.5f) * 256.0f));
        }
}

// ---- logical API ----------------------------------------------------------
void Canvas::fillRect(int32_t x, int32_t y, int32_t rw, int32_t rh, uint16_t c)
{
    if (rw <= 0 || rh <= 0) return;
    int32_t x0 = P(x), y0 = P(y);
    pRect(x0, y0, P(x + rw) - x0, P(y + rh) - y0, c);
}

void Canvas::drawPixel(int32_t x, int32_t y, uint16_t c)     { fillRect(x, y, 1, 1, c); }
void Canvas::drawFastHLine(int32_t x, int32_t y, int32_t l, uint16_t c) { fillRect(x, y, l, 1, c); }
void Canvas::drawFastVLine(int32_t x, int32_t y, int32_t l, uint16_t c) { fillRect(x, y, 1, l, c); }

void Canvas::drawRect(int32_t x, int32_t y, int32_t rw, int32_t rh, uint16_t c)
{
    drawFastHLine(x, y, rw, c);
    drawFastHLine(x, y + rh - 1, rw, c);
    drawFastVLine(x, y, rh, c);
    drawFastVLine(x + rw - 1, y, rh, c);
}

void Canvas::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    if (y0 == y1) { drawFastHLine(min(x0, x1), y0, abs(x1 - x0) + 1, c); return; }
    if (x0 == x1) { drawFastVLine(x0, min(y0, y1), abs(y1 - y0) + 1, c); return; }
    // one logical pixel wide, through the centres of the end pixels
    pWideLine(Pc(x0) * 16 + 8, Pc(y0) * 16 + 8, Pc(x1) * 16 + 8, Pc(y1) * 16 + 8,
              (SCALE_FP * 4) >> 16, c);
}

void Canvas::fillCircle(int32_t x, int32_t y, int32_t r, uint16_t c)
{
    // TFT_eSPI fills every pixel within r inclusive, i.e. out to r + 0.5
    pDisc(Pc(x), Pc(y), Q(r + 0.5f) - 2, c);
}

void Canvas::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint16_t c, uint16_t)
{
    pDisc(Pc(x), Pc(y), Q(r + 0.5f) - 2, c);
}

void Canvas::drawCircle(int32_t x, int32_t y, int32_t r, uint16_t c)
{
    pRing(Pc(x), Pc(y), Q((float)r), (SCALE_FP * 4) >> 16, c);
}

void Canvas::drawSmoothCircle(int32_t x, int32_t y, int32_t r, uint16_t c, uint16_t)
{
    pRing(Pc(x), Pc(y), Q((float)r), 6, c);
}

void Canvas::fillRoundRect(int32_t x, int32_t y, int32_t rw, int32_t rh, int32_t r, uint16_t c)
{
    if (rw <= 0 || rh <= 0) return;
    if (r < 0) r = 0;
    int32_t x0 = P(x), y0 = P(y), x1 = P(x + rw), y1 = P(y + rh);
    int32_t rr = P(r);
    int32_t W = x1 - x0, H = y1 - y0;
    if (2 * rr > W) rr = W / 2;
    if (2 * rr > H) rr = H / 2;
    pRect(x0 + rr, y0, W - 2 * rr, H, c);
    pRect(x0, y0 + rr, rr, H - 2 * rr, c);
    pRect(x1 - rr, y0 + rr, rr, H - 2 * rr, c);
    if (rr > 0) {
        int32_t rq = rr * 4 + 2;
        pDisc(x0 + rr,     y0 + rr,     rq, c);
        pDisc(x1 - 1 - rr, y0 + rr,     rq, c);
        pDisc(x0 + rr,     y1 - 1 - rr, rq, c);
        pDisc(x1 - 1 - rr, y1 - 1 - rr, rq, c);
    }
}

void Canvas::drawRoundRect(int32_t x, int32_t y, int32_t rw, int32_t rh, int32_t r, uint16_t c)
{
    if (rw <= 0 || rh <= 0) return;
    if (r < 0) r = 0;
    drawFastHLine(x + r, y, rw - 2 * r, c);
    drawFastHLine(x + r, y + rh - 1, rw - 2 * r, c);
    drawFastVLine(x, y + r, rh - 2 * r, c);
    drawFastVLine(x + rw - 1, y + r, rh - 2 * r, c);
    if (r > 0) {
        int32_t rq = Q((float)r), tq = (SCALE_FP * 4) >> 16;
        pRing(Pc(x + r),          Pc(y + r),          rq, tq, c, 0x01);
        pRing(Pc(x + rw - 1 - r), Pc(y + r),          rq, tq, c, 0x02);
        pRing(Pc(x + r),          Pc(y + rh - 1 - r), rq, tq, c, 0x04);
        pRing(Pc(x + rw - 1 - r), Pc(y + rh - 1 - r), rq, tq, c, 0x08);
    }
}

void Canvas::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                          int32_t x2, int32_t y2, uint16_t c)
{
    pTriangle(Pc(x0), Pc(y0), Pc(x1), Pc(y1), Pc(x2), Pc(y2), c);
}

void Canvas::drawWideLine(float ax, float ay, float bx, float by, float wd, uint16_t c, uint16_t)
{
    // logical float coords -> 1/16 physical px, through pixel centres
    const float s = SCALE_FP / 65536.0f;
    pWideLine((int32_t)lroundf((ax + 0.5f) * s * 16.0f), (int32_t)lroundf((ay + 0.5f) * s * 16.0f),
              (int32_t)lroundf((bx + 0.5f) * s * 16.0f), (int32_t)lroundf((by + 0.5f) * s * 16.0f),
              Q(wd), c);
}

// ---- text -----------------------------------------------------------------
int16_t Canvas::fontHeight(uint8_t font) { return fontHeightPx(font) * tsize; }

int16_t Canvas::textWidth(const char *s, uint8_t font)
{
    int16_t wsum = 0;
    GlyphInfo g;
    for (; *s; s++)
        if (fontGlyph(font, (uint8_t)*s, g)) wsum += g.width;
    return wsum * tsize;
}

int16_t Canvas::drawString(const char *s, int32_t x, int32_t y, uint8_t font)
{
    int16_t tw = textWidth(s, font), th = fontHeight(font);
    switch (datum) {
        case TC_DATUM: case MC_DATUM: case BC_DATUM: x -= tw / 2; break;
        case TR_DATUM: case MR_DATUM: case BR_DATUM: x -= tw;     break;
        default: break;
    }
    switch (datum) {
        case ML_DATUM: case MC_DATUM: case MR_DATUM: y -= th / 2; break;
        case BL_DATUM: case BC_DATUM: case BR_DATUM: y -= th;     break;
        default: break;
    }
    int32_t px = P(x), py = P(y);

    // Whole string outside the window: skip it without touching a glyph.
    // drawChar rejects individually, but a caption is typically eight or ten
    // characters and this is one compare for all of them. The width is still
    // returned, since callers lay out against it.
    const int32_t pxEnd = P(x + tw), pyEnd = P(y + th);
    if (px >= ox + w || pxEnd <= ox || py >= oy + h || pyEnd <= oy) return tw;

    for (; *s; s++) px += drawChar((uint8_t)*s, px, py, font);
    return tw;
}

int16_t Canvas::drawNumber(long n, int32_t x, int32_t y, uint8_t font)
{
    char b[16];
    snprintf(b, sizeof b, "%ld", n);
    return drawString(b, x, y, font);
}

// Draws one glyph at a physical origin, pixel-doubled (times the text size).
// Returns the physical advance.
// Decode a glyph into a row-per-uint32 bitmap. All three fonts store their
// pixels differently, so unpacking once here means the drawing below has a
// single shape to work with rather than three.
static bool decodeGlyph(const GlyphInfo &g, uint8_t font, uint32_t *rows, int maxRows)
{
    if (g.height > maxRows || g.width > 32) return false;
    for (int i = 0; i < g.height; i++) rows[i] = 0;

    if (font == 1) {                                        // 5x7, column-major
        for (int col = 0; col < 5; col++) {
            uint8_t line = pgm_read_byte(g.data + col);
            for (int row = 0; row < 8 && row < g.height; row++, line >>= 1)
                if (line & 1) rows[row] |= (1u << col);
        }
        return true;
    }
    if (font == 2) {                                        // bitmap rows, MSB first
        int bytesPerRow = (g.width + 6) / 8;
        for (int row = 0; row < g.height; row++)
            for (int k = 0; k < bytesPerRow; k++) {
                uint8_t line = pgm_read_byte(g.data + row * bytesPerRow + k);
                for (int b = 0; b < 8; b++)
                    if (line & (0x80 >> b)) {
                        int c = k * 8 + b;
                        if (c < g.width) rows[row] |= (1u << c);
                    }
            }
        return true;
    }
    // fonts 4 and 6: run-length. High bit set = a run of lit pixels, else a gap.
    int32_t total = g.width * g.height, pc = 0;
    const uint8_t *p = g.data;
    while (pc < total) {
        uint8_t line = pgm_read_byte(p++);
        if (line & 0x80) {
            int run = (line & 0x7F) + 1;
            while (run-- && pc < total) {
                rows[pc / g.width] |= (1u << (pc % g.width));
                pc++;
            }
        } else {
            pc += line + 1;
        }
    }
    return true;
}

int16_t Canvas::drawChar(uint16_t ch, int32_t px, int32_t py, uint8_t font)
{
    GlyphInfo g;
    if (!fontGlyph(font, ch, g)) return 0;

    // Font 6 is already sized for this panel - 48 px against the 26 px of
    // font 4 - so it is drawn one source pixel to one panel pixel. Doubling
    // it would make it enormous, and scaling is exactly what this font exists
    // to avoid: it carries the detail natively instead of having it
    // interpolated in.
    const int32_t m = (font == 6) ? tsize : TEXT_MAG * tsize;

    // Reject a glyph that misses this window before decoding it. A face is
    // redrawn into every window, so a caption outside the current band would
    // otherwise pay its full decode once per band. The advance width is still
    // returned, since the caller uses it to place the next character.
    const int32_t gw = (font == 1 ? 6 : g.width) * m, gh = g.height * m;
    if (px >= ox + w || px + gw <= ox || py >= oy + h || py + gh <= oy)
        return (int16_t)gw;

    // The fonts are bitmaps at a fixed pixel size, and the panel wants them
    // at 1.94x. There is no such bitmap, so each source pixel used to be
    // block-filled into an m x m square - which left every letterform with
    // hard stair-steps, conspicuous beside the anti-aliased hands and ticks
    // drawn around it.
    //
    // Sampling the glyph bilinearly instead gives each output pixel a
    // coverage value from the four source pixels around it, so an edge fades
    // across m pixels rather than jumping. At the sizes these fonts are used
    // that is the difference between drawn type and pixel art.
    uint32_t rows[40];
    const int gwPx = (font == 1 ? 5 : g.width);
    if (!decodeGlyph(g, font, rows, (int)(sizeof rows / sizeof rows[0]))) return (int16_t)gw;

    // Hard pixels: one filled square per lit source pixel, which is what the
    // LCD-imitating faces want. Note this still reads the decoded glyph -
    // filling every cell would draw a solid block, not a letter.
    if (!smoothText) {
        for (int row = 0; row < g.height; row++)
            for (int col = 0; col < gwPx; col++)
                if ((rows[row] >> col) & 1u)
                    pRect(px + col * m, py + row * m, m, m, tfg);
        return (int16_t)gw;
    }

    auto src = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= gwPx || y >= g.height) return 0;
        return (rows[y] >> x) & 1u;
    };

    // Walk the output box, clipped to the window.
    int32_t x0 = px, y0 = py, x1 = px + gwPx * m, y1 = py + g.height * m;
    if (x0 < ox) x0 = ox;
    if (y0 < oy) y0 = oy;
    if (x1 > ox + w) x1 = ox + w;
    if (y1 > oy + h) y1 = oy + h;

    for (int32_t Y = y0; Y < y1; Y++) {
        // Position of this output row inside the glyph, in 8.8 fixed point,
        // taken at the pixel centre and offset back by half a source pixel so
        // the four taps straddle it.
        int32_t fy = (((Y - py) * 256 + 128) / m) - 128;
        int32_t sy = fy >> 8, wy = fy & 255;
        if (fy < 0) { sy = -1; wy = fy + 256; }

        for (int32_t X = x0; X < x1; X++) {
            int32_t fx = (((X - px) * 256 + 128) / m) - 128;
            int32_t sx = fx >> 8, wx = fx & 255;
            if (fx < 0) { sx = -1; wx = fx + 256; }

            int a = src(sx,     sy);
            int b = src(sx + 1, sy);
            int c = src(sx,     sy + 1);
            int d = src(sx + 1, sy + 1);

            // bilinear: top and bottom rows blended, then between them
            int top = a * (256 - wx) + b * wx;
            int bot = c * (256 - wx) + d * wx;
            int cov = (top * (256 - wy) + bot * wy) >> 8;    // 0..256

            if (cov <= 8) continue;
            if (cov >= 248) { put(X, Y, tfg); continue; }
            blend(X, Y, tfg, cov);
        }
    }
    return (int16_t)(font == 1 ? 6 * m : g.width * m);
}
