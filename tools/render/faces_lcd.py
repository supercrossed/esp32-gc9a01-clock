"""Python ports of the two LCD-style faces, casio and retro.

These follow the C++ line for line - same polygons, same inset maths, same
segment geometry - so the preview matches the panel. Constants are read from
the firmware source where they are #defines; the polygon tables and themes
are transcribed because they are struct initialisers, and a comment in each
face file points back here so the two stay in step.
"""
import math
import re
import os
from canvas import Canvas, TL, TC, TR, ML, MC, MR

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "faces"))


def lround(v):
    return int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))


def cdiv(a, b):
    """C integer division: truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def wx_cloud_mono(c, x, y, fg):
    c.fillCircle(x - 6, y + 2, 5, fg)
    c.fillCircle(x + 6, y + 2, 5, fg)
    c.fillCircle(x, y - 2, 7, fg)
    c.fillRect(x - 6, y + 2, 12, 5, fg)


# ========================================================================
#  casio
# ========================================================================
def _casio_polys():
    txt = open(os.path.join(SRC, "face_casio.cpp")).read()
    polys = {}
    for name, body in re.findall(r"POLY_(\w+)\s*\[4\]\s*=\s*\{(.*?)\};", txt, re.S):
        pts = [tuple(map(int, m)) for m in re.findall(r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", body)]
        polys[name] = pts
    themes = {}
    for name, body in re.findall(r"Theme\s+(\w+_T)\s*=\s*\{(.*?)\}", txt, re.S):
        themes[name] = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{4}", body)]
    K = {}
    for n, v in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(-?\(?[-\w]+\)?)", txt):
        v = v.strip("()")
        try:
            K[n] = int(v, 16) if v.lower().startswith("0x") else int(v)
        except ValueError:
            pass
    return polys, themes, K


def point_in_poly(p, x, y):
    inside = False
    j = len(p) - 1
    for i in range(len(p)):
        if (p[i][1] > y) != (p[j][1] > y):
            xc = (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]
            if x < xc:
                inside = not inside
        j = i
    return inside


def measure_cell(poly, R_MASK, BORDER_W):
    rr = (R_MASK - BORDER_W) ** 2
    sx = sy = cnt = 0
    x0 = y0 = 999
    x1 = y1 = -1
    xs = [q[0] for q in poly]
    ys = [q[1] for q in poly]
    for y in range(max(0, min(ys)), min(240, max(ys) + 1)):
        dy = y - 120
        for x in range(max(0, min(xs)), min(240, max(xs) + 1)):
            dx = x - 120
            if dx * dx + dy * dy > rr:
                continue
            if not point_in_poly(poly, x, y):
                continue
            sx += x
            sy += y
            cnt += 1
            x0, x1 = min(x0, x), max(x1, x)
            y0, y1 = min(y0, y), max(y1, y)
    if not cnt:
        return dict(cx=120, cy=120, x0=0, y0=0, x1=239, y1=239)
    return dict(cx=sx // cnt, cy=sy // cnt, x0=x0, y0=y0, x1=x1, y1=y1)


def inset_poly(p, d):
    n = len(p)
    cx = sum(q[0] for q in p) / n
    cy = sum(q[1] for q in p) / n
    ox, oy, ex, ey = [], [], [], []
    for i in range(n):
        j = (i + 1) % n
        vx, vy = p[j][0] - p[i][0], p[j][1] - p[i][1]
        L = math.hypot(vx, vy) or 1.0
        vx, vy = vx / L, vy / L
        nx, ny = -vy, vx
        if nx * (cx - p[i][0]) + ny * (cy - p[i][1]) < 0:
            nx, ny = -nx, -ny
        ox.append(p[i][0] + nx * d)
        oy.append(p[i][1] + ny * d)
        ex.append(vx)
        ey.append(vy)
    out = []
    for i in range(n):
        h = (i + n - 1) % n
        denom = ex[h] * ey[i] - ey[h] * ex[i]
        if abs(denom) < 0.0001:
            out.append((lround(ox[i]), lround(oy[i])))
            continue
        t = ((ox[i] - ox[h]) * ey[i] - (oy[i] - oy[h]) * ex[i]) / denom
        out.append((lround(ox[h] + ex[h] * t), lround(oy[h] + ey[h] * t)))
    return out


def fill_round_poly(c, outer, r, col):
    inn = inset_poly(outer, float(r))
    n = len(inn)
    cx = sum(q[0] for q in inn) / n
    cy = sum(q[1] for q in inn) / n
    for i in range(1, n - 1):
        c.fillTriangle(inn[0][0], inn[0][1], inn[i][0], inn[i][1],
                       inn[i + 1][0], inn[i + 1][1], col)
    for q in inn:
        c.fillCircle(q[0], q[1], r, col)
    for i in range(n):
        a, b = inn[i], inn[(i + 1) % n]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy)
        if L < 0.001:
            continue
        nx, ny = dy / L, -dx / L
        if nx * ((a[0] + b[0]) * 0.5 - cx) + ny * ((a[1] + b[1]) * 0.5 - cy) < 0:
            nx, ny = -nx, -ny
        ax, ay = a[0] + lround(nx * r), a[1] + lround(ny * r)
        bx, by = b[0] + lround(nx * r), b[1] + lround(ny * r)
        c.fillTriangle(a[0], a[1], b[0], b[1], bx, by, col)
        c.fillTriangle(a[0], a[1], bx, by, ax, ay, col)


def draw_cell(c, poly, th, PANEL_R, BORDER_W):
    fill_round_poly(c, poly, PANEL_R, th["border"])
    inner = inset_poly(poly, float(BORDER_W))
    r = PANEL_R - BORDER_W
    fill_round_poly(c, inner, max(2, r), th["panel"])


def mask_to_circle(c, r, col):
    for y in range(240):
        dy = y - 120
        d2 = r * r - dy * dy
        if d2 <= 0:
            c.drawFastHLine(0, y, 240, col)
            continue
        half = int(math.sqrt(d2))
        x0, x1 = 120 - half, 120 + half
        if x0 > 0:
            c.drawFastHLine(0, y, x0, col)
        if x1 < 240:
            c.drawFastHLine(x1, y, 240 - x1, col)


SEG_MAP = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]


def seg7mask(c, x, y, w, h, t, m, on, off):
    """off=None reproduces retro's renderer, which draws lit segments only."""
    half = cdiv(h - t, 2)
    vh = max(1, cdiv(h - 3 * t, 2))
    segs = ((0x01, x + t, y, w - 2 * t, t), (0x20, x, y + t, t, vh),
            (0x02, x + w - t, y + t, t, vh), (0x40, x + t, y + half, w - 2 * t, t),
            (0x10, x, y + half + t, t, vh), (0x04, x + w - t, y + half + t, t, vh),
            (0x08, x + t, y + h - t, w - 2 * t, t))
    for bit, sx, sy, sw, sh in segs:
        if m & bit:
            c.fillRect(sx, sy, sw, sh, on)
        elif off is not None:
            c.fillRect(sx, sy, sw, sh, off)


def seg7(c, x, y, w, h, t, d, on, off):
    seg7mask(c, x, y, w, h, t, SEG_MAP[d] if 0 <= d <= 9 else 0, on, off)


def seg7pair(c, x, y, w, h, t, gap, v, on, off, valid=True):
    seg7(c, x, y, w, h, t, (v // 10) % 10 if valid else -1, on, off)
    seg7(c, x + w + gap, y, w, h, t, v % 10 if valid else -1, on, off)


SEG14 = {'A': 0x00F7, 'D': 0x120F, 'E': 0x00F9, 'F': 0x00F1, 'H': 0x00F6,
         'I': 0x1209, 'M': 0x0536, 'N': 0x2136, 'O': 0x003F, 'R': 0x20F3,
         'S': 0x00ED, 'T': 0x1201, 'U': 0x003E, 'W': 0x2836}


def seg14(c, x, y, w, h, t, m, on, off, bg):
    half = cdiv(h - t, 2)
    vh = max(1, cdiv(h - 3 * t, 2))
    cx = x + cdiv(w - t, 2)
    hw = max(1, cdiv(w - 3 * t, 2))
    c.fillRect(x + t, y, w - 2 * t, t, on if m & 0x0001 else off)
    c.fillRect(x + t, y + h - t, w - 2 * t, t, on if m & 0x0008 else off)
    c.fillRect(x + t, y + half, hw, t, on if m & 0x0040 else off)
    c.fillRect(cx + t, y + half, hw, t, on if m & 0x0080 else off)
    c.fillRect(x, y + t, t, vh, on if m & 0x0020 else off)
    c.fillRect(x, y + half + t, t, vh, on if m & 0x0010 else off)
    c.fillRect(x + w - t, y + t, t, vh, on if m & 0x0002 else off)
    c.fillRect(x + w - t, y + half + t, t, vh, on if m & 0x0004 else off)
    c.fillRect(cx, y + t, t, vh, on if m & 0x0200 else off)
    c.fillRect(cx, y + half + t, t, vh, on if m & 0x1000 else off)
    wd = max(1.5, t * 0.9)
    c.drawWideLine(x + t + 1, y + t + 1, cx - 1, y + half - 1, wd, on if m & 0x0100 else off, bg)
    c.drawWideLine(x + w - t - 1, y + t + 1, cx + t + 1, y + half - 1, wd, on if m & 0x0400 else off, bg)
    c.drawWideLine(cx - 1, y + half + t + 1, x + t + 1, y + h - t - 1, wd, on if m & 0x0800 else off, bg)
    c.drawWideLine(cx + t + 1, y + half + t + 1, x + w - t - 1, y + h - t - 1, wd, on if m & 0x2000 else off, bg)


def seg14word(c, w, cx, cy, dw, dh, dt, gap, on, off, bg):
    n = len(w)
    x = cx - cdiv(n * dw + (n - 1) * gap, 2)
    y = cy - cdiv(dh, 2)
    for i, ch in enumerate(w):
        seg14(c, x + i * (dw + gap), y, dw, dh, dt, SEG14.get(ch, 0), on, off, bg)


DAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]


def face_casio(c, hh, mm, ss, mday, wday, temp_f, night=False, status=0x2E68):
    polys, themes, K = _casio_polys()
    R_MASK, PANEL_R, BORDER_W = K["R_MASK"], K["PANEL_R"], K["BORDER_W"]
    tv = themes["NIGHT_T" if night else "DAY_T"]
    th = dict(shell=tv[0], border=tv[1], panel=tv[2], ink=tv[3], ghost=tv[4])

    B = {n: measure_cell(polys[n], R_MASK, BORDER_W)
         for n in ("TEMP", "DATE", "SEC", "TIME", "DAY", "WX")}

    c.fillScreen(th["shell"])
    for n in ("TEMP", "DATE", "SEC", "TIME", "DAY", "WX"):
        draw_cell(c, polys[n], th, PANEL_R, BORDER_W)

    def label(s, x, y, font):
        c.setTextDatum(TL)
        c.setTextColor(th["ink"], th["panel"])
        c.drawString(s, x, y, font)

    def labelC(s, x, y, font):
        c.setTextDatum(TC)
        c.setTextColor(th["ink"], th["panel"])
        c.drawString(s, x, y, font)

    # TEMP
    b = B["TEMP"]
    labelC("TEMP", b["cx"], b["cy"] - 24, 2)
    dw, dh, dt, dg = 18, 24, 4, 3
    av = abs(temp_f)
    digits = [av // 100, (av // 10) % 10, av % 10] if av >= 100 else [(av // 10) % 10, av % 10]
    n = len(digits)
    wsum = n * dw + (n - 1) * dg
    left = b["cx"] - cdiv(wsum + 14, 2)
    dy = b["cy"] - 4
    for i, d in enumerate(digits):
        seg7(c, left + i * (dw + dg), dy, dw, dh, dt, d, th["ink"], th["ghost"])
    c.drawCircle(left + wsum + 6, dy + 4, 3, th["ink"])
    label("F", left + wsum + 2, dy + 9, 2)

    # DATE
    b = B["DATE"]
    labelC("DATE", b["cx"], b["cy"] - 26, 2)
    seg7pair(c, b["cx"] - cdiv(2 * 16 + 3, 2), b["cy"] - 6, 16, 24, 4, 3, mday, th["ink"], th["ghost"])

    # SECONDS band
    b = B["SEC"]
    label("SECONDS", b["x0"] + 16, b["cy"] - 8, 2)
    seg7pair(c, b["x1"] - 46, b["cy"] - 9, 14, 18, 3, 2, ss, th["ink"], th["ghost"])
    c.fillSmoothCircle(b["cx"] + 6, b["cy"], 3, status, th["panel"])

    # TIME
    b = B["TIME"]
    dw, dh, dt, dg, cw = 40, 46, 7, 5, 7
    total = 4 * dw + 4 * dg + cw
    x = b["cx"] - cdiv(total, 2)
    dy = b["cy"] - cdiv(dh, 2)
    seg7pair(c, x, dy, dw, dh, dt, dg, hh, th["ink"], th["ghost"])
    cx = x + 2 * dw + 2 * dg
    c.fillRect(cx, dy + 12, cw, cw, th["ink"])
    c.fillRect(cx, dy + dh - 19, cw, cw, th["ink"])
    seg7pair(c, cx + cw + dg, dy, dw, dh, dt, dg, mm, th["ink"], th["ghost"])

    # DAY / weather
    b = B["DAY"]
    seg14word(c, DAYS[wday % 7], b["cx"], b["cy"], 16, 26, 3, 3, th["ink"], th["ghost"], th["panel"])
    b = B["WX"]
    wx_cloud_mono(c, b["cx"], b["cy"], th["ink"])

    mask_to_circle(c, R_MASK, th["shell"])
    return B


# ========================================================================
#  retro
# ========================================================================
MONS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]


def _retro_consts():
    txt = open(os.path.join(SRC, "face_retro.cpp")).read()
    K = {}
    for n, v in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(-?\(?[-\w]+\)?)", txt):
        v = v.strip("()")
        try:
            K[n] = int(v, 16) if v.lower().startswith("0x") else int(v)
        except ValueError:
            pass
    themes = {}
    for name, body in re.findall(r"Theme\s+(\w+_T)\s*=\s*\{(.*?)\}", txt, re.S):
        themes[name] = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{4}", body)]
    return K, themes


def face_retro(c, hh, mm, ss, mday, mon, year, wday, temp_f, sunrise, sunset, night=False):
    K, themes = _retro_consts()
    tv = themes["NIGHT_T" if night else "DAY_T"]
    th = dict(panel=tv[0], ink=tv[1], text=tv[2], dim=tv[3])
    C_BG, C_RING = K["C_BG"], K["C_RING"]
    LCD_X, LCD_Y, LCD_W, LCD_H = K["LCD_X"], K["LCD_Y"], K["LCD_W"], K["LCD_H"]
    Y_DATE, Y_WEEK, Y_SUN, Y_TEMP = K["Y_DATE"], K["Y_WEEK"], K["Y_SUN"], K["Y_TEMP"]

    c.fillScreen(C_BG)
    c.drawSmoothCircle(120, 120, 117, C_RING, C_BG)

    # date, font 1 doubled
    c.setTextSize(2)
    c.setTextDatum(TC)
    c.setTextColor(th["text"], C_BG)
    c.drawString(f"{mday:02d} {MONS[mon % 12]} {year:04d}", 120, Y_DATE, 1)
    c.setTextSize(1)

    # week strip
    cw = 24
    x = 120 - (7 * cw) // 2
    c.setTextDatum(TC)
    for d in range(7):
        if d == wday % 7:
            c.fillRoundRect(x + d * cw + 1, Y_WEEK - 3, cw - 2, 14, 3, th["text"])
            c.setTextColor(C_BG, th["text"])
        else:
            c.setTextColor(th["dim"], C_BG)
        c.drawString(DAYS[d], x + d * cw + cw // 2, Y_WEEK, 1)

    # LCD strip
    c.fillRoundRect(LCD_X, LCD_Y, LCD_W, LCD_H, 6, th["panel"])
    wx_cloud_mono(c, LCD_X + 24, LCD_Y + LCD_H // 2, th["ink"])

    dw, dh, dt, dg, cwc = 26, 40, 6, 4, 6
    x = LCD_X + 46
    dy = LCD_Y + 9
    seg7pair(c, x, dy, dw, dh, dt, dg, hh, th["ink"], None)
    cx = x + 2 * dw + 2 * dg
    c.fillRect(cx, dy + 10, cwc, cwc, th["ink"])
    c.fillRect(cx, dy + dh - 16, cwc, cwc, th["ink"])
    seg7pair(c, cx + cwc + dg, dy, dw, dh, dt, dg, mm, th["ink"], None)

    # seconds
    c.setTextDatum(TR)
    c.setTextColor(th["ink"], th["panel"])
    c.drawString("SEC", LCD_X + LCD_W - 8, LCD_Y + 10, 1)
    seg7pair(c, LCD_X + LCD_W - 30, LCD_Y + 26, 10, 18, 3, 2, ss, th["ink"], None)

    # sunrise / sunset
    c.setTextDatum(TC)
    c.setTextColor(th["text"], C_BG)
    c.drawString(f"SUNRISE:{sunrise // 60:02d}:{sunrise % 60:02d} TO "
                 f"SUNSET:{sunset // 60:02d}:{sunset % 60:02d}", 120, Y_SUN, 1)

    # temperature, font 1 tripled
    tmp = str(temp_f)
    numW = len(tmp) * 18
    left = 120 - (numW + 22) // 2
    c.setTextSize(3)
    c.setTextDatum(TL)
    c.setTextColor(th["text"], C_BG)
    c.drawString(tmp, left, Y_TEMP, 1)
    c.setTextSize(1)
    nx = left + numW + 5
    c.drawCircle(nx + 3, Y_TEMP + 5, 3, th["text"])
    c.drawString("F", nx + 9, Y_TEMP + 8, 2)
