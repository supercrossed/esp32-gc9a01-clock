"""Python port of the panel face, mirroring face_panel.cpp element for element."""
import os
import re
from canvas import TL, TC

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "faces", "face_panel.cpp"))
SEG_MAP = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]
MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
WDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]


def _consts():
    txt = open(SRC).read()
    K = {}
    for n, v in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+|-?\d+)", txt):
        K[n] = int(v, 16) if v.lower().startswith("0x") else int(v)
    return K


def face_panel(c, hh, mm, ss, mday, mon, wday, temp_f, sunrise, sunset, night=False):
    K = _consts()
    P = K["C_PANEL_N"] if night else K["C_PANEL_D"]
    G = K["C_GHOST_N"] if night else K["C_GHOST_D"]
    INK, BG, TXT, BAR, FRAME = K["C_INK"], K["C_BG"], K["C_TEXT"], K["C_BAR"], K["C_FRAME"]

    def seg7(x, y, w, h, t, d):
        m = SEG_MAP[d] if 0 <= d <= 9 else 0
        half, vh = (h - t) // 2, max(1, (h - 3 * t) // 2)
        for bit, sx, sy, sw, sh in ((0x01, x + t, y, w - 2 * t, t), (0x20, x, y + t, t, vh),
                                    (0x02, x + w - t, y + t, t, vh), (0x40, x + t, y + half, w - 2 * t, t),
                                    (0x10, x, y + half + t, t, vh), (0x04, x + w - t, y + half + t, t, vh),
                                    (0x08, x + t, y + h - t, w - 2 * t, t)):
            c.fillRect(sx, sy, sw, sh, INK if m & bit else G)

    def seg_text(s, x, y, w, h, t, gap):
        for ch in s:
            if ch == ":":
                q = t
                c.fillRect(x, y + h // 3 - q // 2, q, q, INK)
                c.fillRect(x, y + 2 * h // 3 - q // 2, q, q, INK)
                x += q + gap
            elif ch == "-":
                c.fillRect(x, y + (h - t) // 2, w - 2, t, INK)
                x += w - 2 + gap
            else:
                seg7(x, y, w, h, t, int(ch) if ch.isdigit() else -1)
                x += w + gap

    def seg_width(s, w, t, gap):
        return sum((t if ch == ":" else w - 2 if ch == "-" else w) + gap for ch in s) - gap

    def panel(x, y, w, h):
        c.drawRect(x - 2, y - 2, w + 4, h + 4, FRAME)
        c.fillRect(x + 2, y, w - 4, h, P)
        c.fillRect(x, y + 2, w, h - 4, P)
        c.fillRect(x + 1, y + 1, w - 2, h - 2, P)

    def label(s, x, y, datum=TC, font=2):
        c.setTextDatum(datum)
        c.setTextColor(INK, P)
        c.drawString(s, x, y, font)

    def sun_half(x, y, rising):
        c.fillCircle(x, y, 4, INK)
        c.fillRect(x - 6, y + 1, 13, 4, P)
        c.drawFastHLine(x - 7, y + 2, 15, INK)
        c.drawLine(x - 6, y - 4, x - 4, y - 2, INK)
        c.drawLine(x + 6, y - 4, x + 4, y - 2, INK)
        c.drawFastVLine(x, y - 8, 3, INK)
        dy = -7 if rising else -6
        c.drawPixel(x - 1, y + dy, INK)
        c.drawPixel(x + 1, y + dy, INK)

    def thermometer(x, y):
        c.drawRect(x - 2, y - 8, 5, 11, INK)
        c.fillRect(x - 1, y - 3, 3, 6, INK)
        c.fillCircle(x, y + 5, 3, INK)
        for dy in (-6, -3, 0):
            c.drawFastHLine(x + 4, y + dy, 2, INK)

    def signal_bars(x, y, pct):
        for i in range(4):
            h = 3 + i * 3
            c.fillRect(x + i * 5, y - h, 4, h, INK if pct > i * 25 else G)

    def bell(x, y, col):
        c.fillTriangle(x - 5, y + 3, x + 5, y + 3, x, y - 6, col)
        c.fillRect(x - 6, y + 3, 13, 2, col)
        c.fillRect(x - 1, y + 5, 3, 2, col)

    def moon(cx, cy, r, phase8):
        DK = K["C_MOON_DK"]
        c.fillCircle(cx, cy, r + 3, DK)
        c.drawCircle(cx, cy, r + 3, FRAME)
        if phase8 == 0:
            c.fillCircle(cx, cy, r, 0x4208)
            return
        c.fillCircle(cx, cy, r, K["C_MOON"])
        if phase8 == 1:
            c.fillCircle(cx - r // 2, cy, r, DK)
        elif phase8 == 2:
            c.fillRect(cx - r - 1, cy - r - 1, r + 1, 2 * r + 3, DK)
        elif phase8 == 3:
            c.fillCircle(cx - r - r // 2, cy, r, DK)
        elif phase8 == 5:
            c.fillCircle(cx + r + r // 2, cy, r, DK)
        elif phase8 == 6:
            c.fillRect(cx + 1, cy - r - 1, r + 1, 2 * r + 3, DK)
        elif phase8 == 7:
            c.fillCircle(cx + r // 2, cy, r, DK)

    def hhmm(m):
        return "--:--" if m < 0 else "%02d:%02d" % (m // 60, m % 60)

    def wx_cloud(x, y, col, bg):
        # overcast icon, mono
        c.fillCircle(x - 6, y + 2, 5, col)
        c.fillCircle(x + 6, y + 2, 5, col)
        c.fillCircle(x, y - 2, 7, col)
        c.fillRect(x - 6, y + 2, 12, 5, col)

    c.fillScreen(BG)
    for y in range(2, 240, 4):
        for x in range((2 if y & 4 else 0), 240, 4):
            c.drawPixel(x, y, K["C_DOT"])

    c.setTextDatum(TC)
    c.setTextColor(TXT, BG)
    c.drawString("ESP32", 120, 18, 1)

    panel(40, 30, 76, 32)
    s = hhmm(sunrise)
    seg_text(s, 40 + (76 - seg_width(s, 8, 2, 2)) // 2 - 6, 33, 8, 12, 2, 2)
    label("SUNRISE", 70, 44)
    sun_half(106, 51, True)

    panel(124, 30, 76, 32)
    s = str(temp_f)
    w = seg_width(s, 10, 2, 2)
    x = 124 + (76 - w - 18) // 2
    seg_text(s, x, 33, 10, 14, 2, 2)
    c.setTextDatum(TL)
    c.setTextColor(INK, P)
    c.drawCircle(x + w + 4, 35, 2, INK)
    c.drawString("F", x + w + 8, 32, 2)
    label("TEMP", 152, 46)
    thermometer(186, 52)

    c.drawFastHLine(48, 77, 144, BAR)
    c.setTextDatum(TC)
    c.setTextColor(TXT, BG)
    for pct in (0, 30, 50, 70, 100):
        x = 52 + pct * 136 // 100
        c.drawFastVLine(x, 74, 7, BAR)
        c.drawString(str(pct), x, 65, 1)
    now = hh * 60 + mm
    pct = max(0, min(100, (now - sunrise) * 100 // (sunset - sunrise)))
    x = 52 + pct * 136 // 100
    c.fillTriangle(x - 4, 66, x + 4, 66, x, 73, K["C_RED"])

    panel(30, 84, 38, 34)
    sig = 86
    signal_bars(40, 97, sig)
    s = str(sig)
    w = seg_width(s, 8, 2, 2)
    x = 30 + (38 - w - 9) // 2
    seg_text(s, x, 101, 8, 12, 2, 2)
    c.setTextDatum(TL)
    c.setTextColor(INK, P)
    c.drawString("%", x + w + 2, 99, 2)

    panel(72, 84, 40, 34)
    label(WDAYS[wday], 92, 86)
    label("DAY", 92, 102, TC, 1)

    moon(132, 101, 11, 5)          # waning gibbous for the sample date

    panel(150, 84, 58, 34)
    seg_text("%02d" % mday, 158, 87, 10, 17, 2, 3)
    label(MONTHS[mon], 179, 104)

    c.setTextDatum(TC)
    c.setTextColor(G, BG)
    c.drawString("PA", 43, 124, 2)
    c.setTextColor(TXT, BG)
    c.drawString("24H", 43, 143, 2)
    bell(43, 168, 0x4208)

    panel(58, 124, 130, 52)
    seg_text("%02d:%02d" % (hh, mm), 62, 128, 24, 44, 5, 6)
    panel(190, 124, 30, 52)
    seg_text("%02d" % ss, 194, 154, 10, 18, 2, 3)
    wx_cloud(205, 139, INK, P)

    panel(44, 180, 74, 30)
    s = hhmm(sunset)
    seg_text(s, 44 + (74 - seg_width(s, 8, 2, 2)) // 2 - 6, 183, 8, 12, 2, 2)
    label("SUNSET", 74, 194)
    sun_half(108, 200, False)

    panel(122, 180, 74, 30)
    s = hhmm(sunset - sunrise)
    seg_text(s, 122 + (74 - seg_width(s, 8, 2, 2)) // 2, 183, 8, 12, 2, 2)
    label("DAYLIGHT", 159, 194)

    c.setTextDatum(TC)
    c.setTextColor(P, BG)
    c.drawString("GMT -4", 120, 213, 2)
    c.setTextColor(K["C_YELLOW"], BG)
    c.drawString("TIMEZONE", 120, 230, 1)
    for i in range(5):
        c.fillCircle(66 + i * 5, 224, 1, BAR)
        c.fillCircle(154 + i * 5, 224, 1, BAR)
