// Bring-up test for the Waveshare ESP32-C6 AMOLED: no WiFi, no faces.
// Paints colour bands, some text in each font, a ring at the edge, then
// follows a finger with a dot. If this shows nothing the panel init is
// wrong; if the dot lags the finger or lands elsewhere, touch is.
#include <Arduino.h>
#include "../c6/amoled.h"
#include "../c6/touch.h"
#include "../c6/tft_shim.h"
#include "esp_heap_caps.h"

static const int W = amoled::WIDTH, H = amoled::HEIGHT, BH = 32;
static uint16_t *buf;
static TFT_eSPI  g;
static bool      touchOk;

static void swapPush(int x, int y, int w, int h)
{
    for (int i = 0; i < w * h; i++) buf[i] = (uint16_t)((buf[i] << 8) | (buf[i] >> 8));
    amoled::waitIdle();
    amoled::push(x, y, w, h, buf);
}

template <typename F>
static void full(F draw)
{
    for (int y = 0; y < H; y += BH) {
        g.attach(buf, 0, y, W, BH);
        draw();
        swapPush(0, y, W, BH);
    }
}

static void testCard()
{
    full([] {
        // four horizontal bands in logical space
        g.fillRect(0,   0, 240, 60, 0xF800);
        g.fillRect(0,  60, 240, 60, 0x07E0);
        g.fillRect(0, 120, 240, 60, 0x001F);
        g.fillRect(0, 180, 240, 60, 0xFFFF);
        g.drawSmoothCircle(120, 120, 118, 0xFD20, 0x0000);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(0xFFFF, 0xF800); g.drawString("AMOLED OK", 120, 30, 4);
        g.setTextColor(0x0000, 0x07E0); g.drawString("font 2 abc 123", 120, 90, 2);
        g.setTextColor(0xFFFF, 0x001F); g.drawString("font 1", 120, 150, 1);
        g.setTextColor(0x0000, 0xFFFF); g.drawString(touchOk ? "touch found" : "NO TOUCH", 120, 210, 2);
        g.drawWideLine(20, 20, 220, 220, 3.0f, 0x0000, 0);
    });
}

void setup()
{
    buf = (uint16_t *)heap_caps_malloc(W * BH * 2, MALLOC_CAP_DMA);
    amoled::begin();
    amoled::setBrightness(255);
    touchOk = touch::begin();
    testCard();
}

void loop()
{
    touch::poll();
    static int lx = -1, ly = -1;
    if (touch::pressed()) {
        int x = touch::x(), y = touch::y();
        if (x != lx || y != ly) {
            // a 32x32 window around the finger, drawn in panel space
            int wx = max(0, min(W - 32, x - 16)) & ~1, wy = max(0, min(H - 32, y - 16)) & ~1;
            g.attach(buf, wx, wy, 32, 32);
            g.fillScreen(0x0000);
            g.fillSmoothCircle(x * 240 / W, y * 240 / H, 6, 0xFD20, 0x0000);
            swapPush(wx, wy, 32, 32);
            lx = x; ly = y;
        }
    }
    delay(5);
}
