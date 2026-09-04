// ---------------------------------------------------------------------------
//  The display back end, one per board family.
//
//    tft/screen_tft.cpp     GC9A01 over SPI (S3, C3): a full-frame sprite,
//                           pushed whole.
//    c6/screen_amoled.cpp   CO5300 AMOLED over QSPI (C6): no room for a full
//                           frame, so bands and dirty boxes.
//
//  main.cpp only talks to this.
// ---------------------------------------------------------------------------
#pragma once
#include "face.h"

// What a painter draws into. On the TFT boards it is the real TFT_eSPI; on
// the AMOLED it is the Canvas shim of the same name.
typedef TFT_eSPI GfxDirect;

void screenInit();
void screenClear(uint16_t bg);
// The face for this instant. `hint` overlays the WiFi-setup pill.
void screenRenderFace(const FaceVTable *f, const struct tm &t, float subSec, bool hint);
// A static screen (boot banner, setup instructions). The painter draws all of it.
void screenPaint(void (*painter)(GfxDirect &));
// Tell a band renderer its last-frame knowledge is stale.
void screenInvalidate();
// How often a smooth face should be drawn, in Hz. 0 = as fast as possible.
int  screenSweepHz();
// 0..255 where the panel supports it; ignored elsewhere.
void screenSetBrightness(uint8_t v);

// Screen rotation in quarter turns clockwise, for boards that can do it.
// The AMOLED applies it on the way to the panel; the TFT boards ignore it.
void screenSetRotation(int quarterTurns);
int  screenGetRotation();

// Small pill along the bottom of whatever face is showing, while the hotspot
// is open because the network went away. Shared by both back ends.
template <typename GFX>
inline void drawPortalHint(GFX &g)
{
    g.fillRoundRect(44, 199, 152, 24, 12, 0x0000);
    g.drawRoundRect(44, 199, 152, 24, 12, 0xFD20);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(0xFD20, 0x0000);
    g.drawString("WiFi: Clock-Setup", 120, 211, 2);
}
