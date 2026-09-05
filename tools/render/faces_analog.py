"""Python ports of the classic and modern analog faces.

Palettes and geometry constants are parsed from the face sources; the
drawing is transcribed from the C++ and must be kept in step with it.
"""
import os
import re
import math
from canvas import TL, TC, ML, MC

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "faces"))
WDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]


def _consts(face, theme="DAY"):
    txt = open(os.path.join(SRC, f"face_{face}.cpp")).read()
    K = {}
    for n, v in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(-?\(?[-\w]+\)?)", txt):
        v = v.strip("()")
        try:
            K[n] = int(v, 16) if v.lower().startswith("0x") else int(v)
        except ValueError:
            pass

    # A face with more than one theme keeps its colours in named Palette
    # structs rather than #defines, so the whole set can be swapped at
    # sunset. Pull the requested one out and expose it under the same C_*
    # names the drawing code below already uses.
    m = re.search(r"struct\s+Palette\s*\{([^}]*)\}", txt)
    p = re.search(r"static\s+const\s+Palette\s+" + theme + r"\s*=\s*\{([^}]*)\}", txt)
    if m and p:
        fields = re.findall(r"([a-zA-Z_]\w*)\s*(?:,|;)", m.group(1))
        values = [v for v in re.findall(r"0x[0-9A-Fa-f]+", p.group(1))]
        for name, val in zip(fields, values):
            # dial -> C_DIAL, inkSoft -> C_INK_SOFT
            snake = re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()
            K["C_" + snake] = int(val, 16)
    return K


def _sun(c, x, y, r, col):
    c.fillCircle(x, y, r, col)
    for i in range(8):
        a = math.radians(i * 45)
        s, co = math.sin(a), math.cos(a)
        c.drawLine(x + (r + 2) * s, y - (r + 2) * co, x + (r + 5) * s, y - (r + 5) * co, col)


def _cloud(c, x, y, col):
    c.fillCircle(x - 6, y + 2, 5, col)
    c.fillCircle(x + 6, y + 2, 5, col)
    c.fillCircle(x, y - 2, 7, col)
    c.fillRect(x - 6, y + 2, 12, 5, col)


# --------------------------------------------------------------------------
def face_classic(c, hh, mm, ss, mday, theme="DAY"):
    K = _consts("classic", theme)
    CX = CY = 120
    D, INK, HAND = K["C_DIAL"], K["C_INK"], K["C_HAND"]
    SECOND = K["C_SECOND"]
    SUB_CX, SUB_CY, SUB_R = K["SUB_CX"], K["SUB_CY"], K["SUB_R"]
    ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"]

    c.fillScreen(D)
    c.drawSmoothCircle(CX, CY, K["R_EDGE"], K["C_BEZEL"], D)
    c.drawCircle(CX, CY, K["R_TRACK_O"], INK)
    c.drawCircle(CX, CY, K["R_TRACK_I"], INK)
    for i in range(60):
        a = math.radians(i * 6)
        s, co = math.sin(a), math.cos(a)
        r_in = K["R_HOUR_I"] if i % 5 == 0 else K["R_TRACK_I"]
        x0, y0 = round(CX + r_in * s), round(CY - r_in * co)
        x1, y1 = round(CX + K["R_TRACK_O"] * s), round(CY - K["R_TRACK_O"] * co)
        if i % 5 == 0:
            c.drawWideLine(x0, y0, x1, y1, 2.5, INK, D)
        else:
            c.drawLine(x0, y0, x1, y1, INK)

    num = []
    for i in range(12):
        a = math.radians(i * 30)
        num.append((round(CX + K["R_NUM"] * math.sin(a)), round(CY - K["R_NUM"] * math.cos(a))))
    c.setTextDatum(MC)
    c.setTextColor(INK, D)
    for i in range(12):
        n = 12 if i == 0 else i
        if n in (3, 6):
            continue
        c.drawString(ROMAN[n - 1], num[i][0], num[i][1], 4)

    # date window
    DW, DH = K["DATE_W"], K["DATE_H"]
    x, y = num[3][0] - DW // 2, CY - DH // 2
    c.fillRect(x, y, DW, DH, K["C_DATE_BG"])
    c.drawRect(x - 1, y - 1, DW + 2, DH + 2, K["C_DATE_INK"])
    c.setTextColor(K["C_DATE_INK"], K["C_DATE_BG"])
    c.drawNumber(mday, num[3][0], CY, 2)

    # small seconds
    c.drawCircle(SUB_CX, SUB_CY, SUB_R, INK)
    for i in range(12):
        a = math.radians(i * 30)
        s, co = math.sin(a), math.cos(a)
        c.drawLine(round(SUB_CX + (SUB_R - 4) * s), round(SUB_CY - (SUB_R - 4) * co),
                   round(SUB_CX + SUB_R * s), round(SUB_CY - SUB_R * co), INK)
    c.setTextColor(K["C_INK_SOFT"], D)
    c.drawString("60", SUB_CX, SUB_CY - 13, 1)
    c.drawString("15", SUB_CX + 13, SUB_CY, 1)
    c.drawString("30", SUB_CX, SUB_CY + 13, 1)
    c.drawString("45", SUB_CX - 13, SUB_CY, 1)
    a = math.radians(ss * 6)
    sn, cs = math.sin(a), math.cos(a)
    # Small seconds has its own steel, a step lighter than the main hands.
    c.drawWideLine(SUB_CX - 6 * sn, SUB_CY + 6 * cs,
                   SUB_CX + (SUB_R - 4) * sn, SUB_CY - (SUB_R - 4) * cs, 1.5, SECOND, D)
    c.fillSmoothCircle(SUB_CX, SUB_CY, 2, SECOND, D)

    def breguet(ang, back, ring_r, tip, w):
        s, co = math.sin(ang), math.cos(ang)
        c.drawWideLine(CX - back * s, CY + back * co,
                       CX + (ring_r - 5) * s, CY - (ring_r - 5) * co, w, HAND, D)
        rx, ry = round(CX + ring_r * s), round(CY - ring_r * co)
        c.fillSmoothCircle(rx, ry, 6, HAND, D)
        c.fillSmoothCircle(rx, ry, 3, D, HAND)
        c.drawWideLine(CX + (ring_r + 5) * s, CY - (ring_r + 5) * co,
                       CX + tip * s, CY - tip * co, w * 0.6, HAND, D)

    s = ss
    m = mm + s / 60.0
    h = (hh % 12) + m / 60.0
    breguet(math.radians(h * 30), 10, 56, 72, 5.0)
    breguet(math.radians(m * 6), 12, 84, 104, 3.5)
    c.fillSmoothCircle(CX, CY, 5, HAND, D)
    c.fillSmoothCircle(CX, CY, 2, 0x2E68, HAND)


# --------------------------------------------------------------------------
def face_modern(c, hh, mm, ss, mday, wday, temp_f, is_day, sunset):
    K = _consts("modern")
    CX = CY = 120
    D, SUB, RING = K["C_DIAL"], K["C_SUB"], K["C_SUB_RING"]

    c.fillScreen(K["C_BEZEL"])
    c.fillCircle(CX, CY, K["R_DIAL"], D)
    c.drawSmoothCircle(CX, CY, K["R_DIAL"], RING, K["C_BEZEL"])
    for i in range(60):
        if i % 5 == 0:
            continue
        a = math.radians(i * 6)
        c.fillCircle(round(CX + K["R_DOT"] * math.sin(a)), round(CY - K["R_DOT"] * math.cos(a)), 1, K["C_DOT"])
    for i in range(12):
        a = math.radians(i * 30)
        s, co = math.sin(a), math.cos(a)
        x0, y0 = round(CX + K["R_BAT_I"] * s), round(CY - K["R_BAT_I"] * co)
        x1, y1 = round(CX + K["R_BAT_O"] * s), round(CY - K["R_BAT_O"] * co)
        if i == 0:
            c.drawWideLine(x0 - 4, y0, x1 - 4, y1, 4.0, K["C_INDEX"], D)
            c.drawWideLine(x0 + 4, y0, x1 + 4, y1, 4.0, K["C_INDEX"], D)
        else:
            c.drawWideLine(x0, y0, x1, y1, 4.0, K["C_INDEX"], D)

    def subdial(x, y, r):
        c.fillCircle(x, y, r, SUB)
        c.drawSmoothCircle(x, y, r, RING, D)

    # weather
    WX_CX, WX_CY = K["WX_CX"], K["WX_CY"]
    subdial(WX_CX, WX_CY, K["WX_R"])
    _cloud(c, WX_CX, WX_CY - 8, 0xBDF7)
    buf = str(temp_f)
    c.setTextColor(K["C_TEXT"], SUB)
    tw = c.textWidth(buf, 2)
    left = WX_CX - (tw + 16) // 2
    c.setTextDatum(ML)
    c.drawString(buf, left, WX_CY + 13, 2)
    c.drawCircle(left + tw + 3, WX_CY + 13 - 5, 2, K["C_TEXT"])
    c.drawString("F", left + tw + 7, WX_CY + 13, 2)

    # date tile
    DS = K["DATE_S"]
    x, y = K["DATE_CX"] - DS // 2, K["DATE_CY"] - DS // 2
    c.fillRoundRect(x, y, DS, DS, 8, SUB)
    c.drawRoundRect(x, y, DS, DS, 8, RING)
    c.setTextDatum(TC)
    c.setTextColor(K["C_ACCENT"], SUB)
    c.drawString(WDAYS[wday], K["DATE_CX"], y + 3, 1)
    c.setTextColor(K["C_TEXT"], SUB)
    c.drawNumber(mday, K["DATE_CX"], y + 12, 4)

    # day / night
    SUN_CX, SUN_CY = K["SUN_CX"], K["SUN_CY"]
    subdial(SUN_CX, SUN_CY, K["SUN_R"])
    if is_day:
        _sun(c, SUN_CX, SUN_CY - 6, 8, 0xFDA0)
    else:
        c.fillCircle(SUN_CX, SUN_CY - 6, 9, 0xDEFB)
        c.fillCircle(SUN_CX + 4, SUN_CY - 9, 9, SUB)
    c.setTextDatum(MC)
    c.setTextColor(K["C_TEXT2"], SUB)
    c.drawString(f"{sunset // 60:02d}:{sunset % 60:02d}", SUN_CX, SUN_CY + 12, 1)

    def baton(ang, back, ln, w, lf, lt):
        s, co = math.sin(ang), math.cos(ang)
        c.drawWideLine(CX - back * s, CY + back * co, CX + ln * s, CY - ln * co,
                       w + 2, K["C_HAND_EDGE"], D)
        c.drawWideLine(CX - back * s, CY + back * co, CX + ln * s, CY - ln * co,
                       w, K["C_HAND"], K["C_HAND_EDGE"])
        c.drawWideLine(CX + lf * s, CY - lf * co, CX + lt * s, CY - lt * co,
                       w * 0.4, K["C_LUME"], K["C_HAND"])

    s = ss
    m = mm + s / 60.0
    h = (hh % 12) + m / 60.0
    baton(math.radians(h * 30), 14, 60, 9.0, 14, 52)
    baton(math.radians(m * 6), 16, 92, 7.0, 18, 84)
    a = math.radians(s * 6)
    sn, cs = math.sin(a), math.cos(a)
    c.drawWideLine(CX - 22 * sn, CY + 22 * cs, CX + 100 * sn, CY - 100 * cs, 1.8, K["C_ACCENT"], D)
    c.fillSmoothCircle(round(CX - 22 * sn), round(CY + 22 * cs), 4, K["C_ACCENT"], D)
    c.fillSmoothCircle(CX, CY, 7, K["C_HAND"], D)
    c.fillSmoothCircle(CX, CY, 4, K["C_ACCENT"], K["C_HAND"])
    c.fillSmoothCircle(CX, CY, 2, 0x2E68, K["C_ACCENT"])


# --------------------------------------------------------------------------
def face_california(c, hh, mm, ss, mday, wday, wx):
    """The California dial: Roman above, Arabic below, weather where the
    moonphase would be."""
    K = _consts("california")
    CX = CY = 120
    D, INK, DIM = K["C_DIAL"], K["C_INK"], K["C_INK_DIM"]
    SEC, SUB = K["C_SEC"], K["C_SUB"]
    RN = K["R_NUM"]
    MARKS = [None, "I", "II", None, "4", "5", None, "7", "8", None, "X", "XI"]

    c.fillScreen(D)

    # minute track
    for i in range(60):
        a = math.radians(i * 6)
        s, co = math.sin(a), math.cos(a)
        hour = (i % 5 == 0)
        ri = K["R_TICK_H"] if hour else K["R_TICK_I"]
        x0, y0 = CX + ri * s, CY - ri * co
        x1, y1 = CX + K["R_TICK_O"] * s, CY - K["R_TICK_O"] * co
        if hour:
            c.drawWideLine(x0, y0, x1, y1, 3.5, INK, D)
        else:
            c.drawWideLine(x0, y0, x1, y1, 1.6, DIM, D)

    # numerals and batons
    num = []
    for i in range(12):
        a = math.radians(i * 30)
        num.append((round(CX + RN * math.sin(a)), round(CY - RN * math.cos(a))))
    c.setTextDatum(MC)
    c.setTextColor(INK, D)
    for i in range(12):
        if MARKS[i]:
            c.drawString(MARKS[i], num[i][0], num[i][1], 4)
            continue
        if i == 0:
            continue
        a = math.radians(i * 30)
        s, co = math.sin(a), math.cos(a)
        c.drawWideLine(CX + (RN - 9) * s, CY - (RN - 9) * co,
                       CX + (RN + 9) * s, CY - (RN + 9) * co, 5.0, INK, D)

    # the triangle at 12
    ty = CY - RN
    c.fillTriangle(CX - 11, ty - 10, CX + 11, ty - 10, CX, ty + 10, INK)

    # date under 12
    WD = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
    c.setTextColor(K["C_DATE"], D)
    c.drawString("%s %d" % (WD[wday % 7], mday), CX, CY - 44, 2)

    # weather where a moonphase would sit, with no well around it
    wx(c, CX, K["SUB_CY"], INK, D)

    # hands
    s = ss
    m = mm + s / 60.0
    h = (hh % 12) + m / 60.0
    EDGE = K["C_EDGE"]
    for ang, back, ln, wd, col in ((h * 30, 14, 58, 9.0, EDGE),
                                   (h * 30, 14, 58, 7.0, INK),
                                   (m * 6, 18, 92, 7.0, EDGE),
                                   (m * 6, 18, 92, 5.0, INK),
                                   (s * 6, 30, 100, 1.5, SEC)):
        a = math.radians(ang)
        c.drawWideLine(CX - back * math.sin(a), CY + back * math.cos(a),
                       CX + ln * math.sin(a), CY - ln * math.cos(a), wd, col, D)
    c.fillSmoothCircle(CX, CY, 5, INK, D)
    c.fillSmoothCircle(CX, CY, 2, SEC, INK)


# --------------------------------------------------------------------------
def face_outrun(c, hh, mm, mday, wday, temp_f, sig, theme="NIGHT"):
    """Synthwave sun over a perspective grid. `theme` picks one of the five
    palettes; the firmware crossfades between them on the real sun times."""
    K = _consts("outrun")
    CX = 120
    HZ, SCY, SR = K["HORIZON"], K["SUN_CY"], K["SUN_R"]

    # the palettes, in the order the struct declares them
    import re as _re
    txt = open(os.path.join(SRC, "face_outrun.cpp")).read()
    m = _re.search(r"static const Pal P_" + theme + r" = \{([^}]*)\}", txt)
    vals = [int(v, 16) for v in _re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    skyTop, skyBot, grid, gridDim, ink, sun, sunLow, panel_c, label = vals

    def mix(a, b, t):
        ar, ag, ab = (a >> 11) & 31, (a >> 5) & 63, a & 31
        br, bg, bb = (b >> 11) & 31, (b >> 5) & 63, b & 31
        return (((ar + ((br - ar) * t >> 8)) << 11)
                | ((ag + ((bg - ag) * t >> 8)) << 5)
                | (ab + ((bb - ab) * t >> 8)))

    for y in range(HZ):
        c.drawFastHLine(0, y, 240, mix(skyTop, skyBot, y * 255 // (HZ - 1)))

    # sun, sliced
    for dy in range(-SR, SR + 1):
        y = SCY + dy
        if y < 0 or y >= HZ + 12:
            continue
        if dy > -10 and (((dy + 10) // 4) % 2 == 1):
            continue
        half = int(math.sqrt(max(0, SR * SR - dy * dy)) + 0.5)
        if half <= 0:
            continue
        c.drawFastHLine(CX - half, y, 2 * half,
                        mix(sun, sunLow, (dy + SR) * 255 // (2 * SR)))

    c.fillRect(0, HZ, 240, 240 - HZ, skyBot)
    for i in range(-7, 8):
        c.drawWideLine(CX, HZ, CX + i * 46, 240, 1.4, gridDim, skyBot)
    for i in range(1, 8):
        f = 1.0 - (1.0 - i / 7.0) ** 2
        y = HZ + int((240 - HZ) * f)
        if y >= 240:
            break
        c.drawWideLine(0, y, 240, y, 1.4, grid, skyBot)
    c.drawWideLine(0, HZ, 240, HZ, 2.0, grid, skyBot)

    c.setTextDatum(TC)
    c.setTextColor(label, skyTop)
    c.drawString("O U T R U N", CX, 18, 1)

    WD = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
    c.fillRoundRect(CX - 56, 30, 112, 20, 4, panel_c)
    c.drawFastHLine(CX - 52, 30, 104, label)
    c.drawFastHLine(CX - 52, 49, 104, label)
    c.setTextDatum(MC)
    c.setTextColor(ink, panel_c)
    c.drawString("%s  %d" % (WD[wday % 7], mday), CX, 40, 2)

    SEG = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]

    def digit(x, y, w, h, d, col):
        msk = SEG[d] if 0 <= d <= 9 else 0
        t = 3
        half = (h - t) // 2
        vh = max(1, (h - 3 * t) // 2)
        for bit, bx, by, bw, bh in (
                (0x01, x + t, y, w - 2 * t, t),
                (0x20, x, y + t, t, vh),
                (0x02, x + w - t, y + t, t, vh),
                (0x40, x + t, y + half, w - 2 * t, t),
                (0x10, x, y + half + t, t, vh),
                (0x04, x + w - t, y + half + t, t, vh),
                (0x08, x + t, y + h - t, w - 2 * t, t)):
            if msk & bit:
                c.fillRect(bx, by, bw, bh, col)

    DW, DH, GAP = 26, 42, 5
    x = CX - (4 * DW + 3 * GAP + 10) // 2
    digit(x, 58, DW, DH, hh // 10, ink)
    digit(x + DW + GAP, 58, DW, DH, hh % 10, ink)
    c.fillRect(x + 2 * (DW + GAP) + 2, 58 + DH // 3, 4, 4, ink)
    c.fillRect(x + 2 * (DW + GAP) + 2, 58 + 2 * DH // 3, 4, 4, ink)  # lit half the time
    digit(x + 2 * (DW + GAP) + 10, 58, DW, DH, mm // 10, ink)
    digit(x + 3 * (DW + GAP) + 10, 58, DW, DH, mm % 10, ink)

    for px in (48, 124):
        c.fillRoundRect(px, 176, 66, 28, 4, panel_c)
        c.drawRoundRect(px, 176, 66, 28, 4, label)
    c.setTextDatum(MC)
    c.setTextColor(ink, panel_c)
    c.drawString("%d\xb0" % temp_f, 81, 186, 2)
    c.setTextColor(label, panel_c)
    c.drawString("WEATHER", 81, 197, 1)
    c.setTextColor(ink, panel_c)
    c.drawString("CLOUDY", 157, 186, 2)
    c.setTextColor(label, panel_c)
    c.drawString("CONDITIONS", 157, 197, 1)


# --------------------------------------------------------------------------
def face_orbit(c, hh, mm, ss, mday, wday, temp_f):
    K = _consts("orbit")
    CX = CY = 120
    BG, TR = K["C_BG"], K["C_TRACK"]

    def arc(r, frac, w, col):
        if frac <= 0:
            return
        frac = min(frac, 1.0)
        total = frac * 360.0
        step = max(0.6, min(4.0, math.degrees(2 * math.asin((w * 0.5) / (2.0 * r)))))
        px, py = CX, CY - r
        a = step
        while a <= total + 0.001:
            aa = math.radians(min(a, total))
            x, y = CX + r * math.sin(aa), CY - r * math.cos(aa)
            c.drawWideLine(px, py, x, y, w, col, BG)
            px, py = x, y
            a += step

    def head(r, frac, col):
        a = math.radians(frac * 360.0)
        x, y = round(CX + r * math.sin(a)), round(CY - r * math.cos(a))
        c.fillSmoothCircle(x, y, 7, col, BG)
        c.fillSmoothCircle(x, y, 3, K["C_TEXT"], col)

    c.fillScreen(BG)
    RS, RM, RH = K["R_SEC"], K["R_MIN"], K["R_HOUR"]
    for r in (RS, RM, RH):
        arc(r, 1.0, 8.0, TR)

    sec = ss
    mins = mm + sec / 60.0
    hrs = (hh % 12) + mins / 60.0
    arc(RH, hrs / 12.0, 8.0, K["C_HOUR"])
    arc(RM, mins / 60.0, 8.0, K["C_MIN"])
    arc(RS, sec / 60.0, 8.0, K["C_SEC"])
    head(RH, hrs / 12.0, K["C_HOUR"])
    head(RM, mins / 60.0, K["C_MIN"])
    head(RS, sec / 60.0, K["C_SEC"])

    for i in range(12):
        a = math.radians(i * 30)
        s, co = math.sin(a), math.cos(a)
        c.drawWideLine(CX + (RH - 11) * s, CY - (RH - 11) * co,
                       CX + (RH - 6) * s, CY - (RH - 6) * co,
                       2.5 if i % 3 == 0 else 1.4, K["C_DIM"], BG)

    c.setTextDatum(MC)
    c.setTextColor(K["C_TEXT"], BG)
    c.drawString("%02d:%02d" % (hh, mm), CX, CY - 8, 4)
    WD = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
    c.setTextColor(K["C_DIM"], BG)
    c.drawString("%s %d" % (WD[wday % 7], mday), CX, CY + 16, 2)
    c.setTextColor(K["C_ACCENT"], BG)
    c.drawString("%d\xb0F" % temp_f, CX, CY + 36, 2)
    c.fillSmoothCircle(CX, CY + 56, 3, 0x2E68, BG)
