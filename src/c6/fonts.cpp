#include "fonts.h"
#include <pgmspace.h>

// The tables are TFT_eSPI's .c files renamed .inc so the build system does not
// compile them on their own (PROGMEM is only defined once Arduino.h is in).
#include "fonts/glcdfont.inc"      // static const unsigned char font[]
#include "fonts/Font16.inc"        // widtbl_f16[], chrtbl_f16[]
#include "fonts/Font32rle.inc"     // widtbl_f32[], chrtbl_f32[]

uint8_t fontHeightPx(uint8_t f)
{
    switch (f) {
        case 1:  return 8;
        case 2:  return 16;
        case 4:  return 26;
        default: return 16;
    }
}

bool fontGlyph(uint8_t f, uint16_t ch, GlyphInfo &g)
{
    g.height = fontHeightPx(f);
    if (f == 1) {
        if (ch > 255) return false;
        g.data  = font + ch * 5;
        g.width = 6;
        return true;
    }
    if (ch < 32 || ch > 127) return false;
    int i = ch - 32;
    if (f == 2) {
        g.data  = chrtbl_f16[i];
        g.width = pgm_read_byte(widtbl_f16 + i);
        return true;
    }
    if (f == 4) {
        g.data  = chrtbl_f32[i];
        g.width = pgm_read_byte(widtbl_f32 + i);
        return true;
    }
    return false;
}
