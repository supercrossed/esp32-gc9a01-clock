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

    for (int32_t y = y0; y <= y1; y++) {
        int64_t py = (int64_t)y * 16 + 8;                  // pixel centre, 1/16 px
        for (int32_t x = x0; x <= x1; x++) {
            int64_t px = (int64_t)x * 16 + 8;
            int64_t qx, qy;
            if (l2 == 0) { qx = ax; qy = ay; }
            else {
                int64_t dot = (px - ax) * dx + (py - ay) * dy;
                if (dot <= 0)       { qx = ax; qy = ay; }
                else if (dot >= l2) { qx = bx; qy = by; }
                else {
                    int64_t t = (dot << 16) / l2;          // 16.16
                    qx = ax + ((dx * t) >> 16);
                    qy = ay + ((dy * t) >> 16);
                }
            }
            int64_t ex = px - qx, ey = py - qy;
            int64_t d2 = (ex * ex + ey * ey) / 16;         // 1/256 px^2 -> quarter px^2
            if (d2 >= outer) continue;
            if (d2 <= inner) { put(x, y, c); continue; }
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
    for (int32_t y = miny; y <= maxy; y++)
        for (int32_t x = minx; x <= maxx; x++) {
            int32_t d1 = (x - x1) * (y0 - y1) - (x0 - x1) * (y - y1);
            int32_t d2 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2);
            int32_t d3 = (x - x0) * (y2 - y0) - (x2 - x0) * (y - y0);
            bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos)) put(x, y, c);
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
int16_t Canvas::drawChar(uint16_t ch, int32_t px, int32_t py, uint8_t font)
{
    GlyphInfo g;
    if (!fontGlyph(font, ch, g)) return 0;
    const int32_t m = TEXT_MAG * tsize;

    if (font == 1) {                                        // 5x7, column-major
        for (int col = 0; col < 5; col++) {
            uint8_t line = pgm_read_byte(g.data + col);
            for (int row = 0; row < 8; row++, line >>= 1)
                if (line & 1) pRect(px + col * m, py + row * m, m, m, tfg);
        }
        return 6 * m;
    }
    if (font == 2) {                                        // bitmap rows, MSB first
        int bytesPerRow = (g.width + 6) / 8;
        for (int row = 0; row < g.height; row++)
            for (int k = 0; k < bytesPerRow; k++) {
                uint8_t line = pgm_read_byte(g.data + row * bytesPerRow + k);
                for (int b = 0; b < 8 && line; b++, line <<= 1)
                    if (line & 0x80) pRect(px + (k * 8 + b) * m, py + row * m, m, m, tfg);
            }
        return g.width * m;
    }
    // font 4: run-length. High bit set = run of lit pixels, else a gap.
    int32_t total = g.width * g.height, pc = 0;
    const uint8_t *p = g.data;
    while (pc < total) {
        uint8_t line = pgm_read_byte(p++);
        if (line & 0x80) {
            int run = (line & 0x7F) + 1;
            while (run-- && pc < total) {
                pRect(px + (pc % g.width) * m, py + (pc / g.width) * m, m, m, tfg);
                pc++;
            }
        } else {
            pc += line + 1;
        }
    }
    return g.width * m;
}
