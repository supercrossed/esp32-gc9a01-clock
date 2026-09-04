// ---------------------------------------------------------------------------
//  AMOLED back end. There is no full frame buffer: 466 x 466 x 2 is 434 KB
//  and the chip has about 300 KB for everything. So:
//
//    * A full repaint is rendered in horizontal bands, each drawn into one
//      of two DMA buffers and pushed while the next band is being drawn.
//
//    * Between full repaints, a face that animates supplies the boxes that
//      changed (a sweeping hand, old and new position). Only those are
//      re-rendered and pushed. On top of that, one band of the screen is
//      refreshed every frame in rotation, so the rest of the dial (the
//      minute hand creeping, the date at midnight) never goes stale and
//      there is never a full-frame hitch.
//
//  Faces render through the same call for every window: the canvas clips,
//  so drawing "the whole face" into a 40 x 40 box costs about what the box
//  costs.
// ---------------------------------------------------------------------------
#include "../screen.h"
#include "amoled.h"
#include "esp_heap_caps.h"

bool useSprite = true;                         // faces never need the direct-draw marker here

static const int W = amoled::WIDTH, H = amoled::HEIGHT;
static uint16_t *bufs[2] = {nullptr, nullptr};
static int       bandH   = 0;                  // rows per band buffer
static int       cur     = 0;                  // which buffer is being drawn into
static TFT_eSPI  canvas;                       // the Canvas shim

static const FaceVTable *lastFace = nullptr;
static struct tm prevT;
static float     prevSub = 0;
static bool      havePrev = false;
static bool      hintShown = false;
static int       rollBand = 0;

void screenInit()
{
    amoled::begin();
    // Two DMA-capable band buffers, as tall as the heap allows.
    static const int tries[] = {64, 48, 32, 24, 16};
    for (int bh : tries) {
        bufs[0] = (uint16_t *)heap_caps_malloc(W * bh * 2, MALLOC_CAP_DMA);
        bufs[1] = (uint16_t *)heap_caps_malloc(W * bh * 2, MALLOC_CAP_DMA);
        if (bufs[0] && bufs[1]) { bandH = bh; break; }
        if (bufs[0]) { heap_caps_free(bufs[0]); bufs[0] = nullptr; }
        if (bufs[1]) { heap_caps_free(bufs[1]); bufs[1] = nullptr; }
    }
    amoled::setBrightness(255);
}

// ---- burn-in mitigation ----------------------------------------------------
// An AMOLED ages where it is lit, and this is a clock: several faces hold
// bright elements in one place for the three hours between rotations - the
// dot-matrix field, the casio segments, the delorean readouts. Left alone
// those would eventually shadow.
//
// So the whole image creeps around a small box, a pixel at a time, on a slow
// cycle. At this size and rate it is not visible in use - a pixel every few
// minutes, never more than two from centre - but it keeps any given emitter
// from carrying the same bright edge for hours.
//
// It costs nothing per pixel: the offset is applied to the origin the canvas
// is attached at, so the face draws itself shifted rather than being moved
// afterwards. The pushed rectangle is unchanged, so nothing downstream - the
// rotation, the byte swap, the DMA - has to know about it.
static const uint32_t SHIFT_MS = 3UL * 60UL * 1000UL;  // one step every 3 min
static int      shiftX = 0, shiftY = 0;
static uint32_t lastShift = 0;
// Bands still to be repainted at the new offset. A shift moves everything by
// one pixel, so the old content is wrong everywhere - but forcing a full
// frame to fix it costs 300-500 ms on the busier faces, which is several
// frame slots and reads as a hitch every three minutes. Repainting one band
// per frame instead spreads that over a second, and since the two versions
// differ by a single pixel of offset there is nothing to see while it
// catches up.
static int      shiftBandsLeft = 0;
static int      shiftBand = 0;

// A closed tour around the edge of the box, rather than a random walk: a
// walk revisits the middle far more often than the corners, which is where
// the wear it is meant to spread would then concentrate. This ring visits
// all sixteen positions equally, and every step - including the wrap from
// the last back to the first - moves exactly one pixel, so the creep is
// never a visible jump.
static void stepShift()
{
    if (millis() - lastShift < SHIFT_MS) return;
    lastShift = millis();

    static const int8_t TOUR[][2] = {
        {-2, -2}, {-1, -2}, { 0, -2}, { 1, -2}, { 2, -2}, { 2, -1},
        { 2,  0}, { 2,  1}, { 2,  2}, { 1,  2}, { 0,  2}, {-1,  2},
        {-2,  2}, {-2,  1}, {-2,  0}, {-2, -1},
    };
    static const int N = (int)(sizeof TOUR / sizeof TOUR[0]);
    static int step = 0;
    step = (step + 1) % N;
    shiftX = TOUR[step][0];
    shiftY = TOUR[step][1];

    // Queue the repaint rather than demanding it all at once. havePrev stays
    // true, so the face keeps redrawing only its own dirty boxes - which are
    // now drawn at the new offset - while the rest of the screen is brought
    // over a band at a time.
    shiftBandsLeft = (H + bandH - 1) / bandH;
    shiftBand = 0;
}

// The panel wants each pixel big-endian; the canvas works little-endian.
static void swapBytes(uint16_t *p, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint16_t)((p[i] << 8) | (p[i] >> 8));
}

// ---- screen rotation -------------------------------------------------------
// Quarter turns clockwise. The face renders as it always does, into a window
// in upright coordinates; the turn is applied on the way to the panel.
//
// It has to happen here and not in the canvas: the band renderer clips every
// primitive to the window before a pixel is written, so rotating at the pixel
// would send each band's output outside the strip it was clipped to and most
// of it would vanish. It cannot happen on the panel either - this controller
// supports mirror_x only, not swap_xy or mirror_y, so 90 and 270 degrees are
// not available in hardware.
//
// The 180 degree case is a reversal in place and is folded into the byte
// swap, so it costs nothing extra. The 90 and 270 cases transpose into the
// spare band buffer, which is free at that moment - the previous push has
// already been waited on.
static int rotQuarters = 0;

void screenSetRotation(int q)
{
    q = ((q % 4) + 4) % 4;
    if (q == rotQuarters) return;
    rotQuarters = q;
    havePrev = false;          // dirty boxes from the old orientation are void
}
int screenGetRotation() { return rotQuarters; }

// Hand the current buffer to the panel and switch to the other one.
static void flush(int x, int y, int w, int h)
{
    const int N = W - 1;       // the panel is square, so H - 1 is the same

    if (rotQuarters == 0) {
        swapBytes(bufs[cur], w * h);
        amoled::waitIdle();
        amoled::push(x, y, w, h, bufs[cur]);
        cur ^= 1;
        return;
    }

    if (rotQuarters == 2) {
        // Reverse the pixels and byte-swap them in the same pass.
        uint16_t *p = bufs[cur];
        int n = w * h;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            uint16_t a = p[i], b = p[j];
            p[i] = (uint16_t)((b << 8) | (b >> 8));
            p[j] = (uint16_t)((a << 8) | (a >> 8));
        }
        if (n & 1) { uint16_t v = p[n / 2]; p[n / 2] = (uint16_t)((v << 8) | (v >> 8)); }
        amoled::waitIdle();
        amoled::push(N - (x + w - 1), N - (y + h - 1), w, h, bufs[cur]);
        cur ^= 1;
        return;
    }

    // 90 or 270: transpose into the other buffer, which the push we are about
    // to wait on has finished with.
    amoled::waitIdle();
    uint16_t *src = bufs[cur], *dst = bufs[cur ^ 1];
    for (int j = 0; j < h; j++) {
        const uint16_t *sp = src + j * w;
        for (int i = 0; i < w; i++) {
            uint16_t v = sp[i];
            v = (uint16_t)((v << 8) | (v >> 8));
            // rot 1: (i,j) -> (h-1-j, i)   rot 3: (i,j) -> (j, w-1-i)
            int di = (rotQuarters == 1) ? (h - 1 - j) : j;
            int dj = (rotQuarters == 1) ? i           : (w - 1 - i);
            dst[dj * h + di] = v;
        }
    }
    int nx = (rotQuarters == 1) ? (N - (y + h - 1)) : y;
    int ny = (rotQuarters == 1) ? x                 : (N - (x + w - 1));
    amoled::push(nx, ny, h, w, dst);
    // dst (bufs[cur ^ 1]) is now in flight, so the next window must be drawn
    // into the other one - which is bufs[cur], the source we just consumed.
    // Leaving cur alone is therefore correct here; flipping it would hand the
    // renderer the buffer being clocked out to the panel, which is what made
    // 90 and 270 degrees paint mostly black with fragments of a frame.
}

// Combine dirty boxes that overlap or nearly touch.
//
// A face describes a moving hand as a run of small boxes along it, which is
// the honest description - but each box becomes its own window, and a window
// costs a whole-face render because render() draws the entire face and lets
// the canvas clip. On the default face that is twelve renders a frame for the
// old and new hand positions, and the boxes overlap each other by about 60%,
// so most of those pixels are pushed more than once as well.
//
// Merging a pair whenever their union is no bigger than the two of them put
// together collapses a hand into one or two windows and drops roughly 40% of
// the pixels. The test is what keeps it honest: two boxes at opposite ends of
// the dial have a union far larger than their sum and are left alone, so this
// never trades a big empty rectangle for a couple of small ones.
//
// O(n^2) over at most 32 boxes, once a frame, against a saving of several
// whole-face renders - the arithmetic is free by comparison.
static int mergeRects(DirtyRect *r, int n)
{
    bool changed = true;
    while (changed && n > 1) {
        changed = false;
        for (int i = 0; i < n && !changed; i++) {
            for (int j = i + 1; j < n; j++) {
                int x0 = min(r[i].x, r[j].x);
                int y0 = min(r[i].y, r[j].y);
                int x1 = max(r[i].x + r[i].w, r[j].x + r[j].w);
                int y1 = max(r[i].y + r[i].h, r[j].y + r[j].h);
                int uni  = (x1 - x0) * (y1 - y0);
                int sep  = r[i].w * r[i].h + r[j].w * r[j].h;
                if (uni > sep) continue;

                r[i].x = (int16_t)x0; r[i].y = (int16_t)y0;
                r[i].w = (int16_t)(x1 - x0); r[i].h = (int16_t)(y1 - y0);
                r[j] = r[--n];
                changed = true;
                break;
            }
        }
    }
    return n;
}

// Draw one window of the screen with `draw`, then push it.
template <typename F>
static void window(int x, int y, int w, int h, F draw)
{
    // Attaching at an origin offset by -shift makes the face draw itself
    // shifted by +shift inside the buffer. The window that gets pushed is
    // unchanged.
    canvas.attach(bufs[cur], x - shiftX, y - shiftY, w, h);
    draw(canvas);
    flush(x, y, w, h);
}

template <typename F>
static void fullFrame(F draw)
{
    for (int y = 0; y < H; y += bandH) {
        int h = min(bandH, H - y);
        window(0, y, W, h, draw);
    }
}

// A logical-space rectangle, grown a little, snapped to even panel pixels,
// split into strips no taller than a band buffer.
template <typename F>
static void dirtyWindow(const DirtyRect &r, F draw)
{
    int x0 = Canvas::P(r.x) - 2,        y0 = Canvas::P(r.y) - 2;
    int x1 = Canvas::P(r.x + r.w) + 2,  y1 = Canvas::P(r.y + r.h) + 2;
    x0 &= ~1; y0 &= ~1; x1 = (x1 + 1) & ~1; y1 = (y1 + 1) & ~1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    int w = x1 - x0;
    if (w <= 0 || y1 <= y0) return;
    int maxRows = (W * bandH) / w & ~1;
    for (int y = y0; y < y1; y += maxRows) {
        int h = min(maxRows, y1 - y);
        window(x0, y, w, h, draw);
    }
}

void screenClear(uint16_t bg)
{
    if (!bandH) return;
    fullFrame([bg](TFT_eSPI &g) { g.fillScreen(bg); });
    havePrev = false;
}

void screenRenderFace(const FaceVTable *f, const struct tm &t, float sub, bool hint)
{
    if (!bandH) return;
    stepShift();
    auto draw = [&](TFT_eSPI &g) {
        f->renderDirect(g, t, sub);
        if (hint) drawPortalHint(g);
    };

    bool full = !havePrev || f != lastFace || hint != hintShown || !f->dirty;
    if (!full) {
        DirtyRect rects[32];
        int n = f->dirty(t, sub, prevT, prevSub, rects, 32);
        if (n <= 0) {
            full = true;
        } else {
            n = mergeRects(rects, n);
            for (int i = 0; i < n; i++) dirtyWindow(rects[i], draw);
            // and one band of background refresh, so nothing else goes stale.
            // Skipped where the face says its dirty set is exhaustive: the
            // band is a full-width window, so it costs another whole-face
            // render every frame - more than the dirty boxes themselves on a
            // face that animates between seconds.
            if (!f->dirtyIsComplete) {
                int y = rollBand * bandH;
                window(0, y, W, min(bandH, H - y), draw);
                rollBand = (y + bandH >= H) ? 0 : rollBand + 1;
            } else if (shiftBandsLeft > 0) {
                // A face with a complete dirty set has no rolling band of its
                // own, so the pixel shift borrows one: exactly the bands that
                // owe a repaint, and only until they are done.
                int y = shiftBand * bandH;
                window(0, y, W, min(bandH, H - y), draw);
                shiftBand++;
                shiftBandsLeft--;
            }
        }
    }
    if (full) { fullFrame(draw); rollBand = 0; shiftBandsLeft = 0; }

    prevT = t; prevSub = sub; havePrev = true; lastFace = f; hintShown = hint;

}

void screenPaint(void (*painter)(GfxDirect &))
{
    if (!bandH) return;
    // Static screens creep too. The alarm interface and the setup banners can
    // be up for a long time, and they are exactly the kind of high-contrast
    // fixed layout that would mark a panel.
    stepShift();
    fullFrame([painter](TFT_eSPI &g) { painter(g); });
    havePrev = false;
}

void screenInvalidate()            { havePrev = false; }
// 10 Hz rather than the 8 this ran at. Measured on the panel, a dirty frame
// on the busiest face is about 62 ms against the 100 ms this allows, so the
// frames that carry the sweep have room. The one frame a second that redraws
// the readouts as well is over budget at either rate, so raising it costs
// nothing that was not already late.
int  screenSweepHz()               { return 10; }
void screenSetBrightness(uint8_t v) { amoled::setBrightness(v); }
