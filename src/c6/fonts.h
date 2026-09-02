// The three TFT_eSPI bitmap fonts the faces use (GLCD 5x7 = font 1, Font16 =
// font 2, Font32 RLE = font 4), compiled from the library's own tables so the
// type on this board matches the other two exactly.
#pragma once
#include <Arduino.h>

struct GlyphInfo {
    const uint8_t *data;   // glyph bitmap (format depends on the font)
    uint8_t width;         // advance in pixels
    uint8_t height;
};

// Returns false for characters the font does not have.
bool    fontGlyph(uint8_t font, uint16_t ch, GlyphInfo &out);
uint8_t fontHeightPx(uint8_t font);
