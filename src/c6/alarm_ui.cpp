#include "alarm_ui.h"
#include "../alarms.h"
#include "../screen.h"
#include "audio.h"
#include "canvas.h"
#include <math.h>

namespace alarm_ui {

// Everything here is in the faces' 240 logical units; the canvas scales to
// the 466 px panel. The glass is round, so nothing important goes outside
// radius ~112 of centre (120,120).
static const int CX = 120, CY = 120;
// Content radius, not the glass radius. The panel is a full circle of r=120,
// but a control whose corner sits on the rim is unreadable and hard to hit,
// so everything is laid out inside this and the corners are checked against
// it rather than eyeballed on a square grid.
static const int RSAFE = 108;

// ---- palette --------------------------------------------------------------
static const uint16_t C_BG     = 0x0000;
static const uint16_t C_CARD   = 0x18E3;
static const uint16_t C_TEXT   = 0xFFFF;
static const uint16_t C_DIM    = 0x8410;
static const uint16_t C_ON     = 0x2E68;   // the green the status lamp uses
static const uint16_t C_OFF    = 0x4208;
static const uint16_t C_ACCENT = 0xFD20;
static const uint16_t C_RING   = 0xF800;

enum Screen { LIST, EDIT, SOUNDS, RINGER };
static Screen screen = LIST;
static bool   open_  = false;
static int    editIdx = -1;       // which alarm EDIT is working on
static Alarm  editCopy;           // edited apart, committed on Save
static bool   editIsNew = false;
static int    ringIdx = -1;

// ---- list scrolling -------------------------------------------------------
static int  scroll = 0;           // logical px, LIST and SOUNDS
static int  dragStart = 0, dragScroll = 0;
static bool dragging = false, dragged = false;
static bool slider = false;        // dragging the volume slider

// ---- the time drum --------------------------------------------------------
// Hour and minute are dragged like a phone time picker rather than nudged
// with arrows: the arrows were small targets on a round screen and tapping
// the number itself - the obvious thing to do - did nothing at all.
static const int DRUM_TOP  = 54;   // first of three rows
static const int DRUM_RH   = 30;   // row height; font 4 is 26 px tall
static const int DRUM_ROWS = 3;    // five rows of this height run off the glass
static const int DRUM_SEL  = 1;    // which row is the selection
static int  drumCol = -1;          // 0 = hour, 1 = minute, while dragging
static int  drumAccum = 0;         // leftover drag, in px
static int  drumLastY = 0;

// Flick momentum. A fast drag keeps the column turning after the finger
// lifts, easing to a stop, the way a phone picker does - without it a long
// jump means many separate drags.
static int      flingCol = -1;         // column still coasting, or -1
static float    flingVel = 0.0f;       // px per frame, signed
static uint32_t flingLast = 0;         // millis of the last step
static float    dragVel = 0.0f;        // running estimate while dragging
static uint32_t dragLastMs = 0;

// ---- touch bookkeeping ----------------------------------------------------
// A tap is a press and a release that did not turn into a drag; tracking the
// press stops a flick that ends over a button from firing it.
static bool wasDown = false;
static int  pressX = 0, pressY = 0;
static bool pendingTap = false;
static int  tapX = 0, tapY = 0;

// How loud an audition is, against the alarm's own volume.
static const uint8_t PREVIEW_PCT = 20;

// The volume slider. Wider than the row it replaced - the circle allows about
// 200 px here, and 160 leaves clear padding at both ends while giving 1.6 px
// per percent instead of 1.1, which is what makes it settable by fingertip.
// The painter and the hit test both derive from these, so they cannot drift.
static const int VOL_W = 160;
static const int VOL_X = CX - VOL_W / 2;
static const int VOL_Y = 150;
static const int VOL_H = 10;

static const int ROW_H    = 44;
static const int LIST_TOP = 44;
static const int LIST_BOT = 138;      // clear of the volume slider below it

static inline bool inBox(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// Half-width available inside the circle for a box of height h whose top is
// at y. The binding constraint is the corner furthest from the centre, not
// the mid-line - measuring at the mid-line is what pushes corners off the
// glass. Clamped so the middle rows do not sprawl wider than the design.
static int halfWidthFor(int y, int h)
{
    int top = abs(y - CY), bot = abs(y + h - CY);
    int dy  = top > bot ? top : bot;
    int d2  = RSAFE * RSAFE - dy * dy;
    if (d2 <= 0) return 0;
    int hw = (int)sqrtf((float)d2);
    return hw > 96 ? 96 : hw;
}

// ---------------------------------------------------------------------------
//  painting
// ---------------------------------------------------------------------------
static void drawToggle(GfxDirect &g, int x, int y, bool on)
{
    const int w = 34, h = 18;
    g.fillRoundRect(x, y, w, h, h / 2, on ? C_ON : C_OFF);
    g.fillSmoothCircle(on ? x + w - h / 2 : x + h / 2, y + h / 2, h / 2 - 2,
                       C_TEXT, on ? C_ON : C_OFF);
}

static void paintList(GfxDirect &g)
{
    g.fillScreen(C_BG);

    g.setTextDatum(TC_DATUM);
    g.setTextColor(C_TEXT, C_BG);
    g.drawString("ALARMS", CX, 16, 4);

    // Sound never reports itself over serial on this board (UART0 only), so
    // a failed codec says so here rather than just staying quiet.
    if (!audio::ready()) {
        g.setTextDatum(TC_DATUM);
        g.setTextColor(C_RING, C_BG);
        g.drawString(audio::status(), CX, 38, 1);
    }

    if (alarms.count == 0) {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("No alarms", CX, 104, 4);
        g.drawString("Tap + to add one", CX, 126, 2);
    }

    for (int i = 0; i < alarms.count; i++) {
        int y = LIST_TOP + i * ROW_H - scroll;
        int h = ROW_H - 6;
        if (y + h < LIST_TOP || y > LIST_BOT) continue;      // off-screen

        // Clip the card to the list window as well as the circle, so a row
        // scrolling past the title does not paint over it.
        if (y < LIST_TOP - h || y > LIST_BOT) continue;

        int halfW = halfWidthFor(y, h);
        if (halfW < 44) continue;                             // too near the rim
        int x = CX - halfW, w = halfW * 2;

        g.fillRoundRect(x, y, w, h, 8, C_CARD);

        char buf[8];
        alarmFormat(alarms.list[i], buf, sizeof buf);
        g.setTextDatum(ML_DATUM);
        g.setTextColor(alarms.list[i].on ? C_TEXT : C_DIM, C_CARD);
        // Font 4 is the largest this canvas has - fonts 6 and 7 exist in
        // TFT_eSPI but not here, and asking for one draws nothing at all.
        g.drawString(buf, x + 12, y + 14, 4);

        g.setTextColor(C_DIM, C_CARD);
        g.drawString(audio::soundName((audio::Sound)alarms.list[i].sound),
                     x + 12, y + 29, 2);

        drawToggle(g, x + w - 46, y + h / 2 - 9, alarms.list[i].on);
    }

    // Volume applies to every alarm, so it belongs here rather than in each
    // editor. Placed on a row the circle can still hold at full width.
    g.fillRoundRect(VOL_X, VOL_Y, VOL_W, VOL_H, VOL_H / 2, C_CARD);
    int fill = VOL_W * alarms.volume / 100;
    if (fill > 0) g.fillRoundRect(VOL_X, VOL_Y, fill, VOL_H, VOL_H / 2, C_ACCENT);
    // A knob at the level, so the setting is readable at a glance and there
    // is something obvious to put a finger on.
    g.fillSmoothCircle(VOL_X + fill, VOL_Y + VOL_H / 2, 7, C_TEXT, C_BG);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_DIM, C_BG);
    g.drawString("VOLUME", CX, 138, 1);

    // Done and +, both inside RSAFE on the bottom arc.
    g.fillRoundRect(48, 176, 68, 24, 12, C_CARD);
    g.setTextColor(C_TEXT, C_CARD);
    g.drawString("Done", 82, 188, 2);

    g.fillSmoothCircle(158, 188, 17, C_ACCENT, C_BG);
    g.setTextColor(C_BG, C_ACCENT);
    g.drawString("+", 158, 186, 4);
}

// One column of the time drum. `value` is the current number, `mod` its
// wrap-around. Rows above and below the selection are dimmed and drawn
// smaller, so the selected value reads as the one in focus.
static void drumColumn(GfxDirect &g, int x, int w, int value, int mod)
{
    for (int k = 0; k < DRUM_ROWS; k++) {
        int y   = DRUM_TOP + k * DRUM_RH;
        int off = k - DRUM_SEL;
        int v   = ((value + off) % mod + mod) % mod;

        char b[4];
        snprintf(b, sizeof b, "%02d", v);

        g.setTextDatum(MC_DATUM);
        if (off == 0) {
            g.setTextColor(C_TEXT, C_CARD);
            g.drawString(b, x + w / 2, y + DRUM_RH / 2, 4);
        } else {
            // Two steps of dimming, so the column fades out at its ends.
            // Font 4 is the selected row, so neighbours step down to 2 then 1
            // - using 4 for them too would flatten the hierarchy.
            g.setTextColor(C_DIM, C_BG);
            g.drawString(b, x + w / 2, y + DRUM_RH / 2, 2);
        }
    }
}

static void paintEdit(GfxDirect &g)
{
    g.fillScreen(C_BG);

    const int colW = 48;
    const int hx = 66, mx = 126;
    const int selY = DRUM_TOP + DRUM_SEL * DRUM_RH;

    // The selection band, drawn behind both columns so it reads as one row.
    g.fillRoundRect(hx - 4, selY, (mx + colW) - hx + 8, DRUM_RH, 6, C_CARD);

    drumColumn(g, hx, colW, editCopy.hour,   24);
    drumColumn(g, mx, colW, editCopy.minute, 60);

    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_TEXT, C_CARD);
    g.drawString(":", CX, selY + DRUM_RH / 2 - 2, 4);

    // Sound: a strip that opens the picker, rather than arrows to cycle it.
    g.fillRoundRect(CX - 56, 162, 112, 22, 11, C_CARD);
    g.setTextColor(C_TEXT, C_CARD);
    g.drawString(audio::soundName((audio::Sound)editCopy.sound), CX, 173, 2);

    // Save and Cancel share the lowest row the circle can hold at this size.
    g.fillRoundRect(58, 186, 58, 22, 11, C_ON);
    g.setTextColor(C_TEXT, C_ON);
    g.drawString("Save", 87, 197, 2);

    g.fillRoundRect(124, 186, 58, 22, 11, C_CARD);
    g.setTextColor(C_TEXT, C_CARD);
    g.drawString(editIsNew ? "Cancel" : "Delete", 153, 197, 2);
}

// The sound picker: every sound in a scrolling list, tapping one previews it
// and selects it. Its own screen because the editor cannot also hold eight
// entries on a round display.
static void paintSounds(GfxDirect &g)
{
    g.fillScreen(C_BG);

    const int RH = 26;
    for (int i = 0; i < (int)audio::SOUND_N; i++) {
        int y = 44 + i * RH - scroll;
        if (y + RH < 30 || y > 178) continue;

        int halfW = halfWidthFor(y, RH - 4);
        if (halfW < 40) continue;
        int x = CX - halfW, w = halfW * 2;

        bool sel = (i == editCopy.sound);
        g.fillRoundRect(x, y, w, RH - 4, 6, sel ? C_ON : C_CARD);
        g.setTextDatum(MC_DATUM);
        g.setTextColor(C_TEXT, sel ? C_ON : C_CARD);
        g.drawString(audio::soundName((audio::Sound)i), CX, y + (RH - 4) / 2, 2);
    }

    g.fillRoundRect(CX - 34, 186, 68, 22, 11, C_CARD);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_TEXT, C_CARD);
    g.drawString("Back", CX, 197, 2);
}

static void paintRinger(GfxDirect &g)
{
    g.fillScreen(C_BG);

    // A ring that breathes, so the screen reads as urgent without costing
    // anything much to draw.
    static uint8_t pulse = 0;
    pulse = (uint8_t)(pulse + 7);
    int r = 104 + (int)(5.0f * sinf(pulse * 0.0245f));
    g.drawSmoothCircle(CX, CY, r, C_RING, C_BG);
    g.drawSmoothCircle(CX, CY, r - 3, C_RING, C_BG);

    char buf[8] = "--:--";
    if (ringIdx >= 0 && ringIdx < alarms.count)
        alarmFormat(alarms.list[ringIdx], buf, sizeof buf);

    g.setTextDatum(MC_DATUM);
    g.setTextColor(C_TEXT, C_BG);
    g.setTextSize(2);                 // font 4 doubled; 6 and 7 do not exist here
    g.drawString(buf, CX, 98, 4);
    g.setTextSize(1);
    g.setTextColor(C_ACCENT, C_BG);
    g.drawString("ALARM", CX, 138, 4);

    g.fillRoundRect(CX - 54, 166, 108, 34, 8, C_RING);
    g.setTextColor(C_TEXT, C_RING);
    g.drawString("Dismiss", CX, 183, 4);
}

static void paint(GfxDirect &g)
{
    switch (screen) {
        case LIST:   paintList(g);   break;
        case EDIT:   paintEdit(g);   break;
        case SOUNDS: paintSounds(g); break;
        case RINGER: paintRinger(g); break;
    }
}

// ---------------------------------------------------------------------------
//  actions
// ---------------------------------------------------------------------------
static void commitEdit()
{
    if (editIsNew) {
        if (alarms.count < ALARM_MAX) alarms.list[alarms.count++] = editCopy;
    } else if (editIdx >= 0 && editIdx < alarms.count) {
        alarms.list[editIdx] = editCopy;
    }
    alarmsSave();
    audio::setVolume(alarms.volume);
}

static void deleteEdit()
{
    if (editIdx < 0 || editIdx >= alarms.count) return;
    for (int i = editIdx; i < alarms.count - 1; i++) alarms.list[i] = alarms.list[i + 1];
    alarms.count--;
    alarmsSave();
}

// Set the alarm volume from a finger x on the slider, and keep a sample
// sounding at that level so the change can be heard. The sound is left
// playing after the finger lifts - stopping on release meant the level could
// only ever be judged mid-drag.
static void setVolumeFromX(int x)
{
    int v = (x - VOL_X) * 100 / VOL_W;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    alarms.volume = (uint8_t)v;
    audio::setVolume(alarms.volume);
    if (!audio::playing()) audio::play((audio::Sound)0);
}

static void clampScroll()
{
    int maxScroll = alarms.count * ROW_H - (LIST_BOT - LIST_TOP);
    if (maxScroll < 0) maxScroll = 0;
    if (scroll < 0) scroll = 0;
    if (scroll > maxScroll) scroll = maxScroll;
}

static void tapList(int x, int y)
{
    if (inBox(x, y, 48, 176, 68, 24)) { close(); return; }        // Done

    int dx = x - 158, dy = y - 188;                               // +
    if (dx * dx + dy * dy <= 21 * 21) {
        if (alarms.count >= ALARM_MAX) return;
        editCopy  = Alarm();
        editIsNew = true;
        editIdx   = -1;
        screen    = EDIT;
        audio::stop();
        audio::blip();
        return;
    }

    if (inBox(x, y, VOL_X - 6, VOL_Y - 6, VOL_W + 12, VOL_H + 12)) {   // volume
        setVolumeFromX(x);
        alarmsSave();
        return;
    }

    for (int i = 0; i < alarms.count; i++) {
        int ry = LIST_TOP + i * ROW_H - scroll;
        int h  = ROW_H - 6;
        if (y < ry || y >= ry + h) continue;
        int halfW = halfWidthFor(ry, h);
        if (halfW < 44) return;
        int rx = CX - halfW, w = halfW * 2;

        if (inBox(x, y, rx + w - 52, ry, 52, h)) {                // the toggle
            alarms.list[i].on = !alarms.list[i].on;
            alarmsSave();
            audio::blip();
        } else {                                                  // the row
            editCopy  = alarms.list[i];
            editIdx   = i;
            editIsNew = false;
            screen    = EDIT;
            audio::stop();
            audio::blip();
        }
        return;
    }
}

static void tapEdit(int x, int y)
{
    if (inBox(x, y, CX - 56, 158, 112, 30)) {                    // sound strip
        screen = SOUNDS;
        scroll = 0;
        audio::blip();
        return;
    }
    if (inBox(x, y, 58, 186, 58, 22)) {                          // Save
        audio::stop(); commitEdit(); screen = LIST; clampScroll(); return;
    }
    if (inBox(x, y, 124, 186, 58, 22)) {                         // Cancel / Delete
        audio::stop();
        if (!editIsNew) deleteEdit();
        screen = LIST;
        clampScroll();
        return;
    }
}

static void tapSounds(int x, int y)
{
    if (inBox(x, y, CX - 34, 186, 68, 22)) {                     // Back
        audio::stop();
        screen = EDIT;
        return;
    }
    const int RH = 26;
    for (int i = 0; i < (int)audio::SOUND_N; i++) {
        int ry = 44 + i * RH - scroll;
        if (y < ry || y >= ry + RH - 4) continue;
        if (halfWidthFor(ry, RH - 4) < 40) return;
        editCopy.sound = (uint8_t)i;
        // Auditioning is deliberately quiet - the alarm volume is set for
        // waking someone, which is not what you want held to your ear while
        // picking a sound.
        audio::playPreview((audio::Sound)i, PREVIEW_PCT);
        return;
    }
}

static void tapRinger(int x, int y)
{
    // Generous target: this is pressed by someone half asleep.
    if (inBox(x, y, CX - 70, 156, 140, 54)) {
        audio::stop();
        ringIdx = -1;
        close();
    }
}

// ---------------------------------------------------------------------------
//  the interface main.cpp uses
// ---------------------------------------------------------------------------
void touch(bool down, int px, int py)
{
    if (!open_) return;

    // Already in the rotated frame: touch::x()/y() apply the screen rotation
    // at the source, so doing it again here would undo it.
    int x = px * 240 / 466, y = py * 240 / 466;

    const int drumTop = DRUM_TOP, drumBot = DRUM_TOP + DRUM_ROWS * DRUM_RH;

    if (down && !wasDown) {                      // press
        wasDown = true;
        pressX = x; pressY = y;
        dragged = false;
        drumCol = -1;

        if (screen == EDIT && y >= drumTop && y < drumBot) {
            // Which column the finger landed in. Outside both, it is not a
            // drum drag at all.
            if      (x >= 62  && x < 118) drumCol = 0;
            else if (x >= 122 && x < 178) drumCol = 1;
            drumAccum = 0;
            drumLastY = y;
            flingCol  = -1;            // touching down catches a coasting drum
            dragVel   = 0.0f;
            dragLastMs = millis();
        } else if (screen == LIST &&
                   y >= VOL_Y - 6 && y < VOL_Y + VOL_H + 6 &&
                   x >= VOL_X - 6 && x < VOL_X + VOL_W + 6) {
            slider = true;
            setVolumeFromX(x);
        } else if (screen == LIST || screen == SOUNDS) {
            dragging = true; dragStart = y; dragScroll = scroll;
        }
    } else if (down) {                           // held
        if (drumCol >= 0) {
            // A row of travel is one step. Accumulate so a slow drag still
            // moves, and so a fast one does not skip.
            int dy = y - drumLastY;
            drumLastY = y;
            drumAccum += dy;
            if (abs(y - pressY) > 3) dragged = true;

            // Track speed for the flick. Smoothed, because the panel reports
            // in bursts and a single sample is a poor estimate.
            uint32_t now = millis();
            uint32_t dt  = now - dragLastMs;
            if (dt > 0) {
                float v = (float)dy * 16.0f / (float)dt;   // px per ~16 ms
                dragVel = dragVel * 0.6f + v * 0.4f;
                dragLastMs = now;
            }
            while (drumAccum >= DRUM_RH) {
                drumAccum -= DRUM_RH;
                if (drumCol == 0) editCopy.hour   = (uint8_t)((editCopy.hour + 23) % 24);
                else              editCopy.minute = (uint8_t)((editCopy.minute + 59) % 60);
            }
            while (drumAccum <= -DRUM_RH) {
                drumAccum += DRUM_RH;
                if (drumCol == 0) editCopy.hour   = (uint8_t)((editCopy.hour + 1) % 24);
                else              editCopy.minute = (uint8_t)((editCopy.minute + 1) % 60);
            }
        } else if (slider) {
            dragged = true;                 // never a tap
            setVolumeFromX(x);
        } else if (dragging) {
            if (abs(y - dragStart) > 4) dragged = true;
            scroll = dragScroll + (dragStart - y);
            if (screen == LIST) clampScroll();
            else {
                int maxS = (int)audio::SOUND_N * 26 - 130;
                if (maxS < 0) maxS = 0;
                if (scroll < 0) scroll = 0;
                if (scroll > maxS) scroll = maxS;
            }
        }
    } else if (wasDown) {                        // release
        wasDown  = false;
        dragging = false;
        if (slider) {
            // The sample sounds only while the finger is on the slider.
            slider = false;
            audio::stop();
            alarmsSave();
            dragged = true;
        }

        // A tap on a drum column steps it once, so the picker works by
        // poking as well as dragging.
        if (drumCol >= 0 && !dragged) {
            int band = (pressY - DRUM_TOP) / DRUM_RH;
            if (band < DRUM_SEL) {
                if (drumCol == 0) editCopy.hour   = (uint8_t)((editCopy.hour + 23) % 24);
                else              editCopy.minute = (uint8_t)((editCopy.minute + 59) % 60);
                audio::blip();
            } else if (band > DRUM_SEL) {
                if (drumCol == 0) editCopy.hour   = (uint8_t)((editCopy.hour + 1) % 24);
                else              editCopy.minute = (uint8_t)((editCopy.minute + 1) % 60);
                audio::blip();
            }
            drumCol = -1;
            return;
        }
        // A flick leaves the column coasting; a slow release just stops.
        if (drumCol >= 0 && dragged && fabsf(dragVel) > 2.0f) {
            flingCol  = drumCol;
            flingVel  = dragVel;
            flingLast = millis();
        }
        drumCol = -1;
        if (!dragged) { pendingTap = true; tapX = pressX; tapY = pressY; }
    }
}

// Advance a coasting column. Friction is per elapsed millisecond rather than
// per frame, so the glide is the same length whatever the render loop is
// doing at the time.
static void stepFling()
{
    if (flingCol < 0) return;

    uint32_t now = millis();
    uint32_t dt  = now - flingLast;
    if (dt == 0) return;
    flingLast = now;
    if (dt > 100) dt = 100;              // a long stall must not launch it

    drumAccum += (int)(flingVel * (float)dt / 16.0f);
    while (drumAccum >= DRUM_RH) {
        drumAccum -= DRUM_RH;
        if (flingCol == 0) editCopy.hour   = (uint8_t)((editCopy.hour + 23) % 24);
        else               editCopy.minute = (uint8_t)((editCopy.minute + 59) % 60);
    }
    while (drumAccum <= -DRUM_RH) {
        drumAccum += DRUM_RH;
        if (flingCol == 0) editCopy.hour   = (uint8_t)((editCopy.hour + 1) % 24);
        else               editCopy.minute = (uint8_t)((editCopy.minute + 1) % 60);
    }

    // Exponential decay, then a floor so it settles instead of creeping.
    flingVel *= powf(0.92f, (float)dt / 16.0f);
    if (fabsf(flingVel) < 0.35f) { flingCol = -1; flingVel = 0.0f; drumAccum = 0; }
}

void draw()
{
    if (!open_) return;

    if (screen == EDIT) stepFling();

    // Taps are acted on here rather than in touch(), so a handler runs on the
    // render loop rather than the touch task - it can then repaint, and NVS
    // writes stay off the sampling path.
    if (pendingTap) {
        pendingTap = false;
        switch (screen) {
            case LIST:   tapList(tapX, tapY);   break;
            case EDIT:   tapEdit(tapX, tapY);   break;
            case SOUNDS: tapSounds(tapX, tapY); break;
            case RINGER: tapRinger(tapX, tapY); break;
        }
        if (!open_) return;
    }
    screenPaint(paint);
}

void open()
{
    if (open_) return;
    open_   = true;
    screen  = LIST;
    scroll  = 0;
    wasDown = false; pendingTap = false; dragged = false; dragging = false;
    clampScroll();
    screenInvalidate();
}

void close()
{
    if (!open_) return;
    open_ = false;
    audio::stop();
    screenInvalidate();
}

bool running() { return open_; }
bool ringing() { return open_ && screen == RINGER; }

void ring(int idx)
{
    ringIdx = idx;
    open_   = true;
    screen  = RINGER;
    wasDown = false; pendingTap = false; dragged = false; dragging = false;
    audio::setVolume(alarms.volume);
    if (idx >= 0 && idx < alarms.count)
        audio::play((audio::Sound)alarms.list[idx].sound);
    screenInvalidate();
}

} // namespace alarm_ui
