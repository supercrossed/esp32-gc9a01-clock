// The TFT_eSPI bitmap fonts the faces use, compiled from the library's own
// tables so the type on this board matches the other two boards exactly:
//
//   font 1  GLCD 5x7
//   font 2  Font16
//   font 4  Font32 RLE, 26 px
//   font 6  Font64 RLE, 48 px - digits, colon, minus and a few letters only
//   font 8  FreeSans 24pt, 34 px caps with the full ASCII set
//
// Fonts 6 and 8 exist so the larger type can be drawn at the panel's own
// resolution instead of being a 26 px bitmap doubled: no filtering puts back
// detail the source never had. Font 8 is the one with letters, so it is what
// a dial with Roman numerals needs.
#pragma once
#include <Arduino.h>

struct GlyphInfo {
    const uint8_t *data;   // glyph bitmap (format depends on the font)
    uint8_t width;         // advance in pixels
    uint8_t height;

    // The GFX fonts are proportional and packed MSB-first across rows, with
    // each glyph placed against a baseline rather than a cell. These carry
    // that; the older fonts leave them at zero.
    bool    gfx = false;
    uint8_t bmpW = 0, bmpH = 0;   // the inked box, which is not the advance
    int8_t  xOff = 0, yOff = 0;   // its corner relative to the pen position
};

// Returns false for characters the font does not have.
bool    fontGlyph(uint8_t font, uint16_t ch, GlyphInfo &out);
uint8_t fontHeightPx(uint8_t font);
