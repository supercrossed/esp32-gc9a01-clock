#include "fonts.h"
#include <pgmspace.h>

// The tables are TFT_eSPI's .c files renamed .inc so the build system does not
// compile them on their own (PROGMEM is only defined once Arduino.h is in).
#include "fonts/glcdfont.inc"      // static const unsigned char font[]
#include "fonts/Font16.inc"        // widtbl_f16[], chrtbl_f16[]
#include "fonts/Font32rle.inc"     // widtbl_f32[], chrtbl_f32[]
#include "fonts/Font64rle.inc"     // widtbl_f64[], chrtbl_f64[]

uint8_t fontHeightPx(uint8_t f)
{
    switch (f) {
        case 1:  return 8;
        case 2:  return 16;
        case 4:  return 26;
        // Font 6 is 48 px tall and holds digits, colon, minus and a couple of
        // letters - nothing else. It exists so the big readouts can be drawn
        // at the panel's own resolution instead of being a 26 px bitmap blown
        // up: no amount of filtering puts detail back that the source never
        // had, which is why doubled font 4 stayed soft however it was scaled.
        case 6:  return 48;
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
    if (f == 6) {
        g.data  = chrtbl_f64[i];
        g.width = pgm_read_byte(widtbl_f64 + i);
        return true;
    }
    return false;
}
