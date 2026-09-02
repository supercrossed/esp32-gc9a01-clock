"""Python port of the pcb face, mirroring face_pcb.cpp element for element.

The palette is parsed from the face source; the geometry is transcribed
because most of it is hand-placed literals. Keep the two in step.
"""
import os
import re
from canvas import TL, TC

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "faces", "face_pcb.cpp"))

SEG_MAP = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]


def _consts():
    txt = open(SRC).read()
    K = {}
    for n, v in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(-?\(?[-\w]+\)?)", txt):
        v = v.strip("()")
        try:
            K[n] = int(v, 16) if v.lower().startswith("0x") else int(v)
        except ValueError:
            pass
    return K


def seg7(c, x, y, w, h, t, d, on, off, draw_off):
    m = SEG_MAP[d] if 0 <= d <= 9 else 0
    half = (h - t) // 2
    vh = max(1, (h - 3 * t) // 2)
    segs = ((0x01, x + t, y, w - 2 * t, t), (0x20, x, y + t, t, vh),
            (0x02, x + w - t, y + t, t, vh), (0x40, x + t, y + half, w - 2 * t, t),
            (0x10, x, y + half + t, t, vh), (0x04, x + w - t, y + half + t, t, vh),
            (0x08, x + t, y + h - t, w - 2 * t, t))
    for bit, sx, sy, sw, sh in segs:
        if m & bit:
            c.fillRect(sx, sy, sw, sh, on)
        elif draw_off:
            c.fillRect(sx, sy, sw, sh, off)


def face_pcb(c, hh, mm, ss):
    K = _consts()
    MOD_W, MOD_H, MOD_GAP, MOD_X0, MOD_Y = K["MOD_W"], K["MOD_H"], K["MOD_GAP"], K["MOD_X0"], K["MOD_Y"]
    PIN_N, PIN_LEN, BUS_TOP, BUS_BOT = K["PIN_N"], K["PIN_LEN"], K["BUS_TOP"], K["BUS_BOT"]

    def modX(i):
        return MOD_X0 + i * (MOD_W + MOD_GAP)

    def via(x, y):
        c.fillCircle(x, y, 3, K["C_VIA"])
        c.fillCircle(x, y, 1, K["C_PCB"])

    def resistor(x, y, ref, refX):
        c.fillRect(x, y, 14, 6, K["C_SMD"])
        c.fillRect(x, y, 3, 6, K["C_SMD_CAP"])
        c.fillRect(x + 11, y, 3, 6, K["C_SMD_CAP"])
        c.setTextDatum(TL)
        c.setTextColor(K["C_SILK_DIM"], K["C_PCB"])
        c.drawString(ref, refX, y - 1, 1)

    def capacitor(x, y):
        c.fillRect(x, y, 8, 5, K["C_CERAMIC"])
        c.fillRect(x, y, 2, 5, K["C_SMD_CAP"])
        c.fillRect(x + 6, y, 2, 5, K["C_SMD_CAP"])

    def ic(x, y, ref, refX):
        c.fillRect(x, y, 22, 12, K["C_IC"])
        c.drawRect(x, y, 22, 12, K["C_MOD_EDGE"])
        for k in range(4):
            c.fillRect(x + 3 + k * 5, y - 3, 2, 3, K["C_PIN_DK"])
            c.fillRect(x + 3 + k * 5, y + 12, 2, 3, K["C_PIN_DK"])
        c.fillCircle(x + 3, y + 3, 1, K["C_SILK_DIM"])
        c.setTextDatum(TL)
        c.setTextColor(K["C_SILK_DIM"], K["C_PCB"])
        c.drawString(ref, refX, y + 2, 1)

    def corner_led(x, y):
        c.fillSmoothCircle(x, y, 16, K["C_LED_HALO"], K["C_PCB"])
        c.fillSmoothCircle(x, y, 9, K["C_LED_HALO2"], K["C_LED_HALO"])
        c.fillSmoothCircle(x, y, 4, K["C_LED"], K["C_LED_HALO2"])
        c.fillSmoothCircle(x - 1, y - 1, 1, K["C_LED_HI"], K["C_LED"])

    def module(i, digit):
        mx, my = modX(i), MOD_Y
        for k in range(PIN_N):
            px = mx + 4 + k * 6
            c.fillRect(px - 1, my - PIN_LEN - 1, 1, PIN_LEN, K["C_PIN_DK"])
            c.fillRect(px, my - PIN_LEN - 1, 2, PIN_LEN, K["C_PIN"])
            c.fillRect(px - 1, my + MOD_H + 1, 1, PIN_LEN, K["C_PIN_DK"])
            c.fillRect(px, my + MOD_H + 1, 2, PIN_LEN, K["C_PIN"])
        c.fillRoundRect(mx, my, MOD_W, MOD_H, 3, K["C_MOD"])
        c.drawRoundRect(mx, my, MOD_W, MOD_H, 3, K["C_MOD_EDGE"])
        c.fillRect(mx + 4, my + 5, 26, 36, K["C_LENS"])
        seg7(c, mx + 7, my + 8, 20, 30, 4, digit, K["C_SEG"], K["C_GHOST"], True)
        seg7(c, mx + 6, my + 7, 22, 32, 6, digit, K["C_SEG_GLOW"], 0, False)
        seg7(c, mx + 7, my + 8, 20, 30, 4, digit, K["C_SEG"], 0, False)

    c.fillScreen(K["C_CASE"])
    c.fillSmoothCircle(120, 120, 112, K["C_PCB"], K["C_CASE"])
    c.drawSmoothCircle(120, 120, 112, K["C_PCB_EDGE"], K["C_CASE"])

    T = K["C_TRACE"]
    c.drawFastHLine(44, BUS_TOP, 152, T)
    c.drawFastHLine(44, BUS_BOT, 152, T)
    for i in range(4):
        for k in range(PIN_N):
            px = modX(i) + 4 + k * 6
            c.drawFastVLine(px, BUS_TOP, MOD_Y - PIN_LEN - 1 - BUS_TOP, T)
            c.drawFastVLine(px, MOD_Y + MOD_H + PIN_LEN + 1,
                            BUS_BOT - (MOD_Y + MOD_H + PIN_LEN + 1), T)
    c.drawFastVLine(44, 60, BUS_TOP - 60, T)
    c.drawFastVLine(196, 60, BUS_TOP - 60, T)
    c.drawFastVLine(44, BUS_BOT, 180 - BUS_BOT, T)
    c.drawFastVLine(196, BUS_BOT, 180 - BUS_BOT, T)
    c.drawFastVLine(28, 110, 20, T); c.drawFastHLine(28, 120, 12, T)
    c.drawFastVLine(212, 110, 20, T); c.drawFastHLine(200, 120, 12, T)
    c.drawFastVLine(96, 49, 11, T); c.drawLine(96, 60, 104, 68, T)
    c.drawFastVLine(104, 68, BUS_TOP - 68, T)
    c.drawFastVLine(144, 49, 11, T); c.drawLine(144, 60, 136, 68, T)
    c.drawFastVLine(136, 68, BUS_TOP - 68, T)
    c.drawFastVLine(75, 76, BUS_TOP - 76, T)
    c.drawFastVLine(151, 76, BUS_TOP - 76, T)
    c.drawFastVLine(120, BUS_BOT, 14, T)
    c.drawFastHLine(96, 177, 11, T)
    c.drawFastHLine(133, 177, 11, T)
    for x, y in ((44, BUS_TOP), (196, BUS_TOP), (44, BUS_BOT), (196, BUS_BOT),
                 (28, 110), (28, 130), (212, 110), (212, 130),
                 (96, 49), (144, 49), (104, 68), (136, 68)):
        via(x, y)

    c.setTextDatum(TC)
    c.setTextColor(K["C_SILK_DIM"], K["C_PCB"])
    c.drawString("2609CLK-4D-0042", 120, 22, 1)
    c.setTextDatum(TL)
    c.setTextColor(K["C_SILK"], K["C_PCB"])
    c.drawString("CHR", 64, 34, 1)
    c.setTextDatum(TC)
    c.drawString("AM/PM", 120, 158, 1)
    c.setTextColor(K["C_SILK_DIM"], K["C_PCB"])
    c.drawString("REV A", 120, 204, 1)

    resistor(64, 44, "R1", 80)
    resistor(64, 54, "R2", 80)
    resistor(150, 44, "R3", 166)
    capacitor(100, 44)
    capacitor(112, 44)
    c.fillRect(150, 55, 12, 6, K["C_SMD_CAP"])
    c.setTextDatum(TL)
    c.setTextColor(K["C_SILK_DIM"], K["C_PCB"])
    c.drawString("Y1", 166, 54, 1)
    ic(64, 64, "U1", 90)
    ic(140, 64, "U2", 166)

    c.fillRect(107, 168, 26, 18, K["C_SWITCH"])
    c.drawRect(107, 168, 26, 18, K["C_MOD_EDGE"])
    for k in range(4):
        c.fillRect(110 + k * 6, 173, 4, 8, K["C_SLIDER"])
    c.fillRect(90, 172, 6, 6, K["C_PAD"])
    c.fillRect(144, 172, 6, 6, K["C_PAD"])
    c.setTextColor(K["C_SILK_DIM"], K["C_PCB"])
    c.drawString("0", 80, 171, 1)
    c.drawString("0", 152, 171, 1)

    for x, y in ((44, 52), (196, 52), (44, 188), (196, 188)):
        corner_led(x, y)

    module(0, hh // 10)
    module(1, hh % 10)
    module(2, mm // 10)
    module(3, mm % 10)

    if ss & 1:
        c.fillSmoothCircle(120, 120, 7, K["C_LED_HALO2"], K["C_PCB"])
        c.fillSmoothCircle(120, 120, 3, K["C_LED"], K["C_LED_HALO2"])
    else:
        c.fillSmoothCircle(120, 120, 3, K["C_LED_HALO"], K["C_PCB"])
