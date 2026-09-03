"""Python port of the delorean face, mirroring face_delorean.cpp."""
import os
import re
import math
from canvas import TL, TC

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "faces", "face_delorean.cpp"))
SEG_MAP = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]
MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN",
          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
WDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
MASK14 = {
    'A': 0x00F7, 'B': 0x128F, 'C': 0x0039, 'D': 0x120F, 'E': 0x00F9, 'F': 0x00F1,
    'G': 0x00BD, 'H': 0x00F6, 'I': 0x1209, 'J': 0x001E, 'K': 0x2470, 'L': 0x0038,
    'M': 0x0536, 'N': 0x2136, 'O': 0x003F, 'P': 0x00F3, 'Q': 0x203F, 'R': 0x20F3,
    'S': 0x00ED, 'T': 0x1201, 'U': 0x003E, 'V': 0x0C30, 'W': 0x2836, 'X': 0x2D00,
    'Y': 0x1500, 'Z': 0x0C09,
}


def _consts():
    txt = open(SRC).read()
    K = {}
    for n, v in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+|-?\d+)", txt):
        K[n] = int(v, 16) if v.lower().startswith("0x") else int(v)
    return K


def face_delorean(c, hh, mm, ss, mday, mon, year, wday, temp_f, sunrise, sunset):
    K = _consts()
    CASE, FRAME, PANEL = K["C_CASE"], K["C_FRAME"], K["C_PANEL"]
    RED, RED_OFF = K["C_RED"], K["C_RED_OFF"]
    GRN, GRN_OFF = K["C_GRN"], K["C_GRN_OFF"]
    AMB, AMB_OFF = K["C_AMB"], K["C_AMB_OFF"]

    def seg7(x, y, w, h, t, d, on, off):
        m = SEG_MAP[d] if 0 <= d <= 9 else 0
        half, vh = (h - t) // 2, max(1, (h - 3 * t) // 2)
        for bit, sx, sy, sw, sh in ((0x01, x + t, y, w - 2 * t, t), (0x20, x, y + t, t, vh),
                                    (0x02, x + w - t, y + t, t, vh),
                                    (0x40, x + t, y + half, w - 2 * t, t),
                                    (0x10, x, y + half + t, t, vh),
                                    (0x04, x + w - t, y + half + t, t, vh),
                                    (0x08, x + t, y + h - t, w - 2 * t, t)):
            c.fillRect(sx, sy, sw, sh, on if m & bit else off)

    def seg14(x, y, w, h, t, m, on, off):
        half, vh = (h - t) // 2, max(1, (h - 3 * t) // 2)
        mx, hw = x + (w - t) // 2, (w - 2 * t) // 2
        for bit, sx, sy, sw, sh in ((0x0001, x + t, y, w - 2 * t, t),
                                    (0x0002, x + w - t, y + t, t, vh),
                                    (0x0004, x + w - t, y + half + t, t, vh),
                                    (0x0008, x + t, y + h - t, w - 2 * t, t),
                                    (0x0010, x, y + half + t, t, vh),
                                    (0x0020, x, y + t, t, vh),
                                    (0x0040, x + t, y + half, hw, t),
                                    (0x0080, mx, y + half, hw, t),
                                    (0x0200, mx, y + t, t, vh),
                                    (0x1000, mx, y + half + t, t, vh)):
            c.fillRect(sx, sy, sw, sh, on if m & bit else off)
        for bit, x0, y0, x1, y1 in ((0x0100, x + t, y + t, mx, y + half - 1),
                                    (0x0400, x + w - t, y + t, mx + t, y + half - 1),
                                    (0x0800, x + t, y + h - t, mx, y + half + t),
                                    (0x2000, x + w - t, y + h - t, mx + t, y + half + t)):
            if m & bit:
                c.drawLine(x0, y0, x1, y1, on)
                c.drawLine(x0 + 1, y0, x1 + 1, y1, on)

    def digits(s, x, y, w, h, t, gap, on, off):
        for ch in s:
            seg7(x, y, w, h, t, int(ch) if ch.isdigit() else -1, on, off)
            x += w + gap

    def letters(s, x, y, w, h, t, gap, on, off):
        for ch in s:
            seg14(x, y, w, h, t, MASK14.get(ch, 0), on, off)
            x += w + gap

    def tab(s, cx, y, w):
        c.fillRect(cx - w // 2, y, w, 11, RED)
        c.setTextDatum(TC)
        c.setTextColor(K["C_LABEL"], RED)
        c.drawString(s, cx, y + 1, 1)

    def well(x, y, w, h):
        c.fillRect(x, y, w, h, PANEL)
        c.drawRect(x, y, w, h, FRAME)

    def flux(cx, cy, r, step):
        c.fillRect(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8, K["C_FLUX_BG"])
        c.drawRect(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8, FRAME)
        jy = cy + 4
        for a, ang in enumerate((240.0, 300.0, 90.0)):
            rad = math.radians(ang)
            sx, sy = math.cos(rad), math.sin(rad)
            lit = (a == step)
            reach = r - 6 if a == 2 else r
            ex, ey = cx + int(sx * r), jy + int(sy * reach)
            c.drawLine(cx, jy, ex, ey, K["C_FLUX_OFF"])
            for i in range(1, 4):
                px = cx + int(sx * r * i / 3.6)
                py = jy + int(sy * reach * i / 3.6)
                c.fillCircle(px, py, 2, K["C_FLUX"] if lit else K["C_FLUX_OFF"])
        c.fillCircle(cx, jy, 3, K["C_FLUX"])

    def gauge(x, y, w, h, pct):
        BGC, INK = K["C_GAUGE_BG"], K["C_GAUGE_INK"]
        c.fillRect(x, y, w, h, BGC)
        c.drawRect(x, y, w, h, FRAME)
        cx, cy = x + w // 2, y + h - 6
        r = h - 18
        for a in range(210, 331, 4):
            rad = math.radians(a)
            c.drawPixel(cx + int(math.cos(rad) * r), cy + int(math.sin(rad) * r), INK)
        for a in range(210, 331, 30):
            rad = math.radians(a)
            c.drawLine(cx + int(math.cos(rad) * (r - 4)), cy + int(math.sin(rad) * (r - 4)),
                       cx + int(math.cos(rad) * r), cy + int(math.sin(rad) * r), INK)
        for a in range(310, 331, 2):
            rad = math.radians(a)
            c.drawLine(cx + int(math.cos(rad) * (r - 3)), cy + int(math.sin(rad) * (r - 3)),
                       cx + int(math.cos(rad) * r), cy + int(math.sin(rad) * r), RED)
        na = math.radians(210 + pct * 120 / 100)
        nx = cx + int(math.cos(na) * (r - 5))
        ny = cy + int(math.sin(na) * (r - 5))
        c.drawLine(cx, cy, nx, ny, K["C_NEEDLE"])
        c.drawLine(cx + 1, cy, nx, ny, K["C_NEEDLE"])
        c.fillCircle(cx, cy, 2, INK)
        c.setTextDatum(TC)
        c.setTextColor(INK, BGC)
        c.drawString("PLUTONIUM", cx, y + 3, 1)

    def trefoil(cx, cy, r):
        Y = K["C_YELLOW"]
        c.fillCircle(cx, cy, r, Y)
        for i in range(3):
            a = math.radians(i * 120 + 90)
            c.fillTriangle(cx, cy,
                           cx + math.cos(a - 0.5) * r * 2, cy + math.sin(a - 0.5) * r * 2,
                           cx + math.cos(a + 0.5) * r * 2, cy + math.sin(a + 0.5) * r * 2, Y)
        c.fillCircle(cx, cy, r // 2, K["C_BG"])

    c.fillScreen(K["C_BG"])
    c.fillRoundRect(14, 22, 212, 196, 8, CASE)
    c.drawRoundRect(14, 22, 212, 196, 8, FRAME)

    flux(68, 58, 22, ss % 3)
    gauge(118, 32, 74, 46, 86)
    trefoil(108, 88, 4)

    tab("TEMP", 188, 84, 40)
    well(168, 96, 40, 22)
    digits("%2d" % temp_f, 174, 99, 13, 16, 3, 4, RED, RED_OFF)

    tab("HOUR", 40, 96, 34)
    tab("MIN", 78, 96, 34)
    tab("SEC", 116, 96, 34)
    tab("YEAR", 156, 96, 40)

    well(23, 108, 34, 24)
    digits("%02d" % hh, 26, 111, 13, 18, 3, 3, RED, RED_OFF)
    well(61, 108, 34, 24)
    digits("%02d" % mm, 64, 111, 13, 18, 3, 3, RED, RED_OFF)
    well(99, 108, 34, 24)
    digits("%02d" % ss, 102, 111, 13, 18, 3, 3, RED, RED_OFF)
    well(137, 108, 50, 24)
    digits("%04d" % year, 140, 111, 10, 18, 3, 2, RED, RED_OFF)

    tab("WEEK", 44, 138, 40)
    tab("DAY", 86, 138, 34)
    tab("MONTH", 134, 138, 52)
    tab("SUNSET", 190, 138, 44)

    well(23, 150, 42, 24)
    letters(WDAYS[wday], 26, 153, 11, 18, 3, 2, GRN, GRN_OFF)
    well(69, 150, 34, 24)
    digits("%02d" % mday, 72, 153, 13, 18, 3, 3, GRN, GRN_OFF)
    well(107, 150, 42, 24)
    letters(MONTHS[mon], 110, 153, 11, 18, 3, 2, GRN, GRN_OFF)
    well(153, 150, 52, 24)
    digits("%02d%02d" % (sunset // 60, sunset % 60), 156, 153, 11, 18, 3, 2, GRN, GRN_OFF)

    tab("SUNRISE", 52, 180, 56)
    tab("DAYLIGHT", 118, 180, 60)
    tab("COND", 186, 180, 44)

    well(23, 192, 58, 22)
    digits("%02d%02d" % (sunrise // 60, sunrise % 60), 26, 195, 12, 16, 3, 2, AMB, AMB_OFF)
    well(89, 192, 58, 22)
    d = sunset - sunrise
    digits("%02d%02d" % (d // 60, d % 60), 92, 195, 12, 16, 3, 2, AMB, AMB_OFF)
    well(155, 192, 50, 22)
    letters("OVC", 158, 195, 14, 16, 3, 2, AMB, AMB_OFF)

    c.setTextDatum(TL)
    c.setTextColor(K["C_DIM"], CASE)
    c.drawString("BT", 24, 84, 1)
    c.fillCircle(38, 87, 2, 0x2E68)
    c.setTextDatum(TC)
    c.setTextColor(K["C_TEXT"], K["C_BG"])
    c.drawString("OUTATIME", 120, 8, 1)
