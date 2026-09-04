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

// The panel wants each pixel big-endian; the canvas works little-endian.
static void swapBytes(uint16_t *p, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint16_t)((p[i] << 8) | (p[i] >> 8));
}

// Hand the current buffer to the panel and switch to the other one.
static void flush(int x, int y, int w, int h)
{
    swapBytes(bufs[cur], w * h);
    amoled::waitIdle();
    amoled::push(x, y, w, h, bufs[cur]);
    cur ^= 1;
}

// Draw one window of the screen with `draw`, then push it.
template <typename F>
static void window(int x, int y, int w, int h, F draw)
{
    canvas.attach(bufs[cur], x, y, w, h);
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
            }
        }
    }
    if (full) { fullFrame(draw); rollBand = 0; }

    prevT = t; prevSub = sub; havePrev = true; lastFace = f; hintShown = hint;
}

void screenPaint(void (*painter)(GfxDirect &))
{
    if (!bandH) return;
    fullFrame([painter](TFT_eSPI &g) { painter(g); });
    havePrev = false;
}

void screenInvalidate()            { havePrev = false; }
int  screenSweepHz()               { return 8; }     // 28,800 bph, like an automatic
void screenSetBrightness(uint8_t v) { amoled::setBrightness(v); }
