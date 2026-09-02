// ---------------------------------------------------------------------------
//  Waveshare ESP32-C6-Touch-AMOLED-1.43: the CO5300 panel over QSPI, driven
//  through ESP-IDF's esp_lcd layer the way Waveshare's own example does it
//  (the SH8601 driver, which the CO5300 is command-compatible with).
//
//  Pushes are asynchronous DMA transfers. Render into one buffer while the
//  other is on its way to the panel, and call waitIdle() before reusing one.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace amoled {

static const int WIDTH  = 466;
static const int HEIGHT = 466;

bool begin();
// Queue a w*h block of RGB565 pixels (big-endian, see screen_amoled.cpp) at
// panel coordinates (x, y). x, y, w and h should be even.
void push(int x, int y, int w, int h, const uint16_t *pixels);
// Block until the last push has left the buffer.
void waitIdle();
// 0..255, the controller's own brightness command; there is no backlight.
void setBrightness(uint8_t v);

} // namespace amoled
