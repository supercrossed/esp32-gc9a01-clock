// ---------------------------------------------------------------------------
//  Canvas: the drawing surface for the AMOLED board.
//
//  The faces were written against TFT_eSPI's sprite API at 240x240. That
//  library cannot drive a QSPI AMOLED and does not build for the ESP32-C6,
//  so this class provides the same calls, rendered by our own rasteriser.
//  Two things happen on the way to the pixels:
//
//    * Coordinates are in the faces' 240-pixel logical space and are scaled
//      to the 466-pixel panel here, so hands, rings and ticks are drawn crisp
//      at the panel's real resolution rather than upscaled afterwards. Text
//      uses the same bitmap fonts, pixel-doubled.
//
//    * The canvas is a window onto the panel, not a whole frame. A full
//      466x466 frame is 434 KB and the chip has nowhere to put it, so the
//      screen is rendered in horizontal bands, and between full frames only
//      small boxes around a moving hand are redrawn. Every primitive clips
//      itself to the window with a bounding-box test first, so the parts of
//      a face that fall outside cost almost nothing.
//
//  Anti-aliasing is integer only: the chip has no floating-point unit, and
//  coverage is derived from squared distances so there is no square root in
//  any per-pixel loop. Smooth shapes blend against what is already in the
//  buffer, which is what the faces' `bg` arguments were approximating.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

enum { TL_DATUM = 0, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM,
       BL_DATUM, BC_DATUM, BR_DATUM };

class Canvas {
public:
    static constexpr int PANEL   = 466;             // physical size
    static constexpr int LOGICAL = 240;             // what the faces draw in
    // scale as 16.16 fixed point, and the integer text magnification
    static constexpr int32_t SCALE_FP = (int32_t)((PANEL * 65536LL + LOGICAL / 2) / LOGICAL);
    static constexpr int     TEXT_MAG = 2;

    // Point the canvas at a buffer holding w*h pixels whose top-left sits at
    // panel coordinates (ox, oy).
    void attach(uint16_t *buf, int ox, int oy, int w, int h);

    // ---- state ----
    void setTextColor(uint16_t fg)              { tfg = fg; tbg = fg; }
    void setTextColor(uint16_t fg, uint16_t bg) { tfg = fg; tbg = bg; }
    void setTextDatum(uint8_t d)                { datum = d; }
    // Text is anti-aliased by default. A face whose whole point is that it
    // looks like a cheap LCD - the retro and dot-matrix ones - turns it off,
    // because smoothing the type there erases the effect being imitated.
    void setTextSmooth(bool on)                 { smoothText = on; }
    void setTextSize(uint8_t s)                 { tsize = s < 1 ? 1 : s; }

    // ---- fills and lines (logical coordinates) ----
    void fillScreen(uint16_t c);
    void fillSprite(uint16_t c)                 { fillScreen(c); }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t c);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t c);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c);
    void drawPixel(int32_t x, int32_t y, uint16_t c);
    void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t c);
    void drawCircle(int32_t x, int32_t y, int32_t r, uint16_t c);
    void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint16_t c, uint16_t bg = 0);
    void drawSmoothCircle(int32_t x, int32_t y, int32_t r, uint16_t c, uint16_t bg = 0);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint16_t c);
    void drawWideLine(float ax, float ay, float bx, float by, float wd,
                      uint16_t c, uint16_t bg = 0);

    // ---- text ----
    int16_t textWidth(const char *s, uint8_t font = 2);
    int16_t textWidth(const String &s, uint8_t font = 2) { return textWidth(s.c_str(), font); }
    int16_t fontHeight(uint8_t font = 2);
    int16_t drawString(const char *s, int32_t x, int32_t y, uint8_t font = 2);
    int16_t drawString(const String &s, int32_t x, int32_t y, uint8_t font = 2)
        { return drawString(s.c_str(), x, y, font); }
    int16_t drawNumber(long n, int32_t x, int32_t y, uint8_t font = 2);

    // ---- window ----
    int  winX() const { return ox; }
    int  winY() const { return oy; }
    int  winW() const { return w; }
    int  winH() const { return h; }
    uint16_t *pixels() { return buf; }

    // logical -> physical, for callers that need to size windows
    static inline int32_t P(int32_t v)  { return (int32_t)(((int64_t)v * SCALE_FP + 32768) >> 16); }

private:
    uint16_t *buf = nullptr;
    int ox = 0, oy = 0, w = 0, h = 0;
    uint16_t tfg = 0xFFFF, tbg = 0x0000;
    uint8_t  datum = TL_DATUM, tsize = 1;
    bool     smoothText = true;

    // centre of a logical pixel, in physical pixels / in 1/16 physical px
    static inline int32_t Pc(int32_t v)   { return (int32_t)(((int64_t)(2 * v + 1) * SCALE_FP / 2 + 32768) >> 16); }
    static inline int32_t P16f(float v)   { return (int32_t)lroundf(v * (SCALE_FP / 65536.0f) * 16.0f); }
    static inline int32_t Q(float v)      { return (int32_t)lroundf(v * (SCALE_FP / 65536.0f) * 4.0f); }

    // physical rasterisers. rq / tq / wq are in quarter pixels.
    void pRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
    void pDisc(int32_t cx, int32_t cy, int32_t rq, uint16_t c);
    void pRing(int32_t cx, int32_t cy, int32_t rq, int32_t tq, uint16_t c, uint8_t quadrants = 0x0F);
    void pWideLine(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t wq, uint16_t c);
    void pTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t c);
    inline void put(int32_t x, int32_t y, uint16_t c) {
        x -= ox; y -= oy;
        if ((uint32_t)x < (uint32_t)w && (uint32_t)y < (uint32_t)h) buf[y * w + x] = c;
    }
    inline void blend(int32_t x, int32_t y, uint16_t c, int32_t a256);
    int16_t drawChar(uint16_t ch, int32_t px, int32_t py, uint8_t font);
};
