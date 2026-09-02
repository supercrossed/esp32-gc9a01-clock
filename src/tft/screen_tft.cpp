// GC9A01 back end: TFT_eSPI with a full-screen sprite, falling back to
// drawing straight at the panel if the 115 KB sprite cannot be allocated.
#include "../screen.h"

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite fb  = TFT_eSprite(&tft);
bool        useSprite = false;

void screenInit()
{
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(0x0000);

    // 240*240*2 = 115200 bytes, claimed before WiFi comes up so the heap is
    // still unfragmented. If it fails we still run, just slower.
    fb.setColorDepth(16);
    useSprite = fb.createSprite(240, 240) != nullptr;
}

void screenClear(uint16_t bg)
{
    if (useSprite) fb.fillSprite(bg);
    tft.fillScreen(bg);
}

void screenRenderFace(const FaceVTable *f, const struct tm &t, float sub, bool hint)
{
    if (useSprite) {
        f->renderSprite(fb, t, sub);
        if (hint) drawPortalHint(fb);
        fb.pushSprite(0, 0);
    } else {
        f->renderDirect(tft, t, sub);
        if (hint) drawPortalHint(tft);
    }
}

void screenPaint(void (*painter)(GfxDirect &)) { painter(tft); }
void screenInvalidate()                         { }
int  screenSweepHz()                            { return 0; }
void screenSetBrightness(uint8_t)               { }
