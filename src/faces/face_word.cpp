// ---------------------------------------------------------------------------
//  Face: "word"  -  word clock, IT IS HALF PAST TEN
//
//  An 11x10 letter grid where the time is spelled out by lighting the letters
//  that form the phrase. Everything else stays visible but dim, so the grid
//  reads as a physical panel with the relevant words illuminated - which is
//  what makes a word clock work rather than just being text on a screen.
//
//  The grid is the classic QLOCKTWO arrangement. Its whole trick is that every
//  phrase must appear as contiguous letters at fixed positions, with filler
//  letters (the stray B, Z, X, M) plugging the gaps. That constraint is why
//  the layout cannot be rearranged casually: move one word and several others
//  stop fitting.
//
//  Resolution is five minutes, as with any word clock. The four corner dots
//  carry the remainder, so the exact minute is still readable.
// ---------------------------------------------------------------------------
#include "../face.h"
#include <string.h>

// Wrapped in its own namespace so every face can be linked into the same
// binary: each defines faceInit/faceRender/faceBackground/faceSmooth, which
// would otherwise be six duplicate global symbols.
namespace face_word {

// ---- palette --------------------------------------------------------------
#define C_BG    0x0000
#define C_OFF   0x18E3   // dormant letters: present, but clearly unlit
#define C_ON    0xFFFF   // lit letters
#define C_DOT   0x07FF   // minute remainder dots
#define C_WARN  0xFD20

// ---- grid -----------------------------------------------------------------
// The grid is a DISC, not a rectangle. Row lengths are derived from the
// circle itself - the chord width at each row's y, divided by the column
// pitch - so they run 8, 10, 11, 12, 12, 12, 12, 11, 10, 8 and the letter
// block genuinely fills the round dial instead of sitting in it as a square.
//
// That constraint drove the word layout rather than the other way round:
// TEN and OCLOCK cannot share the 8-wide bottom row, and TWELVE/EIGHT share
// their E in row 8 to fit ten columns. Every row is exactly its chord width;
// the stray B C D J Q X Y Z are filler.
//
// SPACING is a third constraint, separate again from placement and order:
// two words that light together must not be physically adjacent, or they run
// into each other - PASTTEN, TWENTYFIVE. Every row therefore carries filler
// between any pair that can co-occur. TEN and NINE sit flush on row 4 on
// purpose: both are hours, so they never light at the same time.
//
// READING ORDER is a separate constraint from word placement, and an easy one
// to miss: lit letters render in grid order, so a word placed earlier in the
// grid is read first no matter what the phrase intends. The minute TEN in
// row 3 and the hour TEN in row 4 are therefore SEPARATE spans - putting the
// hour above PAST/TO made 22 of the 288 times read "TEN TO" instead of
// "TO TEN". Any future move of a word must be re-checked for order, not just
// for whether the letters still spell it.
#define ROWS 10
#define COLS 12          // widest row; the lit mask uses this as its stride
#define CW   19          // column pitch
#define CH   20          // row pitch
#define GY   (120 - (ROWS * CH) / 2 + CH / 2)

static const char *GRID[ROWS] = {
    "ITLISAMP",       //  8
    "ACQUARTERD",     // 10
    "TWENTYXFIVE",    // 11
    "HALFXTENYTOZ",   // 12
    "PASTXTENNINE",   // 12
    "ONESIXTHREEQ",   // 12
    "FOURFIVETWOJ",   // 12
    "SEVENELEVEN",    // 11
    "TWELVEIGHT",     // 10
    "XO'CLOCK",       //  8  - apostrophe is part of the grid, as on a real one
};

// Left edge of a row, centred on the dial for that row's own length - this is
// what makes the block round rather than left-aligned.
static inline int rowX(int r) { return 120 - ((int)strlen(GRID[r]) * CW) / 2 + CW / 2; }

// ---- words ----------------------------------------------------------------
// Every phrase fragment, as a span in the grid. Verified offline against all
// 288 five-minute slots.
struct Span { uint8_t row, col, len; };
enum {
    W_IT, W_IS, W_A, W_QUARTER, W_TWENTY, W_FIVEM, W_HALF, W_TENM, W_TO,
    W_PAST, W_NINE, W_ONE, W_SIX, W_THREE, W_FOUR, W_FIVE, W_TWO,
    W_EIGHT, W_ELEVEN, W_SEVEN, W_TWELVE, W_TEN, W_OCLOCK, W_COUNT
};
static const Span WORD[W_COUNT] = {
    {0,0,2},  {0,3,2},  {1,0,1},  {1,2,7},          // IT IS A QUARTER
    {2,0,6},  {2,7,4},  {3,0,4},  {3,5,3},  {3,9,2},// TWENTY FIVE HALF TEN TO
    {4,0,4},  {4,8,4},                              // PAST NINE
    {5,0,3},  {5,3,3},  {5,6,5},                    // ONE SIX THREE
    {6,0,4},  {6,4,4},  {6,8,3},                    // FOUR FIVE TWO
    {8,5,5},  {7,5,6},                              // EIGHT ELEVEN
    {7,0,5},  {8,0,6},                              // SEVEN TWELVE
    {4,5,3},  {9,1,7},                              // TEN (hour) O'CLOCK
};

// Hour index -> word. 0 = twelve, so the array is offset by design.
static const uint8_t HOUR_W[12] = {
    W_TWELVE, W_ONE, W_TWO, W_THREE, W_FOUR, W_FIVE,
    W_SIX, W_SEVEN, W_EIGHT, W_NINE, W_TEN, W_ELEVEN
};

uint16_t faceBackground() { return C_BG; }
bool     faceSmooth()     { return false; }
void     faceInit()       { }

// Build the lit-letter mask for a time. Returns via `lit`, one byte per cell.
static void buildPhrase(const struct tm &t, uint8_t *lit)
{
    memset(lit, 0, COLS * ROWS);

    uint8_t words[8];
    int n = 0;
    words[n++] = W_IT;
    words[n++] = W_IS;

    int slot = (t.tm_min / 5) * 5;
    int hr   = t.tm_hour % 12;

    switch (slot) {
        case 0:                                                   break;
        case 5:  words[n++] = W_FIVEM;   words[n++] = W_PAST;     break;
        case 10: words[n++] = W_TENM;    words[n++] = W_PAST;     break;
        case 15: words[n++] = W_A; words[n++] = W_QUARTER;
                 words[n++] = W_PAST;                             break;
        case 20: words[n++] = W_TWENTY;  words[n++] = W_PAST;     break;
        case 25: words[n++] = W_TWENTY;  words[n++] = W_FIVEM;
                 words[n++] = W_PAST;                             break;
        case 30: words[n++] = W_HALF;    words[n++] = W_PAST;     break;
        // past the half hour the phrasing flips to "to" and the hour advances
        case 35: words[n++] = W_TWENTY;  words[n++] = W_FIVEM;
                 words[n++] = W_TO;   hr++;                       break;
        case 40: words[n++] = W_TWENTY;  words[n++] = W_TO; hr++; break;
        case 45: words[n++] = W_A; words[n++] = W_QUARTER;
                 words[n++] = W_TO;   hr++;                       break;
        case 50: words[n++] = W_TENM;    words[n++] = W_TO; hr++; break;
        case 55: words[n++] = W_FIVEM;   words[n++] = W_TO; hr++; break;
    }

    words[n++] = HOUR_W[hr % 12];
    if (slot == 0) words[n++] = W_OCLOCK;

    for (int i = 0; i < n; i++) {
        const Span &s = WORD[words[i]];
        for (int c = 0; c < s.len; c++)
            lit[s.row * COLS + s.col + c] = 1;
    }
}

// ---- the face -------------------------------------------------------------
template <typename GFX>
static void render(GFX &g, const struct tm &t, float subSec)
{
    (void)subSec;

    uint8_t lit[COLS * ROWS];
    buildPhrase(t, lit);

    g.fillScreen(C_BG);
    g.setTextDatum(MC_DATUM);

    for (int r = 0; r < ROWS; r++) {
        int y  = GY + r * CH;
        int x0 = rowX(r);
        int n  = (int)strlen(GRID[r]);
        for (int c = 0; c < n; c++) {
            int x = x0 + c * CW;
            bool on = lit[r * COLS + c];
            g.setTextColor(on ? (timeValid ? C_ON : C_WARN) : C_OFF, C_BG);
            char s[2] = { GRID[r][c], 0 };
            // Font 4 rather than 2, and lit letters are drawn twice with a
            // one-pixel offset to fake a bold weight - the built-in fonts
            // have no bold, and the lit phrase needs to separate clearly
            // from the dormant grid.
            g.drawString(s, x, y, 4);
            if (on) g.drawString(s, x + 1, y, 4);
        }
    }

    // Minute remainder, on an arc around the bottom rim.
    //
    // These sat in a straight line under the grid and clipped the descenders
    // of O'CLOCK. Curving them onto the perimeter uses space the letter block
    // cannot reach, so nothing has to move. A 30 degree spread at radius 112
    // is what clears the bottom row: at a narrower spread the outer two dots
    // stay under the text and still collide.
    int rem = t.tm_min % 5;
    for (int i = 0; i < 4; i++) {
        // Minus, not plus: screen angles run clockwise from the +x axis,
        // so adding put index 0 on the right and the dots filled leftward.
        float a = (90.0f - (i - 1.5f) * 30.0f) * DEG_TO_RAD;
        int x = 120 + (int)(112 * cosf(a));
        int y = 120 + (int)(112 * sinf(a));
        if (i < rem) g.fillSmoothCircle(x, y, 4, C_DOT, C_BG);
        else         g.drawCircle(x, y, 3, C_OFF);
    }
}

static void faceRender(TFT_eSprite &g, const struct tm &t, float sub) { render(g, t, sub); }
static void faceRender(TFT_eSPI    &g, const struct tm &t, float sub) { render(g, t, sub); }

} // namespace face_word

// Registration: the one symbol this file exposes.
const FaceVTable FACE_WORD = {
    "word",
    face_word::faceInit,
    face_word::faceBackground,
    face_word::faceSmooth,
    (void(*)(TFT_eSprite&, const struct tm&, float))face_word::faceRender,
    (void(*)(TFT_eSPI&,    const struct tm&, float))face_word::faceRender,
};
