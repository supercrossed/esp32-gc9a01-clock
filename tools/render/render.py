"""Render every watch face to a PNG for the README.

Each face is redrawn here in Python, but the numbers come from the firmware:
palettes, geometry and letter grids are parsed out of the .cpp files rather
than copied, so an edit to a face changes these images too. What is NOT shared
is the drawing code itself, so treat these as accurate previews rather than
as screenshots - the real article is on the panel.

    python tools/render/render.py [outdir]
"""
import os
import re
import sys
import math
from canvas import Canvas, TL, TC, TR, ML, MC, MR
import faces_lcd
import faces_pcb
import faces_analog
import faces_panel
import faces_delorean

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "faces"))


def consts(face):
    """Pull #define NAME <value> out of a face's source.

    Values may be plain numbers or small arithmetic over #defines already
    seen - the faces use both, e.g. the dot-matrix digit block is defined
    relative to the size of the field around it. Anything that is not a
    simple integer expression over known names is skipped, as before.
    """
    txt = open(os.path.join(SRC, f"face_{face}.cpp")).read()
    out = {}
    for name, val in re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)\s+([^\n]+)", txt):
        # Drop a trailing // comment, but not a division: only "//" ends a
        # value. Excluding "/" outright truncates expressions like
        # ((COLS - DIG_COLS) / 2) at the operator.
        val = val.split("//")[0].strip()
        v = val.strip("()").strip()
        try:
            out[name] = int(v, 16) if v.lower().startswith("0x") else int(v)
            continue
        except ValueError:
            pass
        # An expression: evaluate it against the names already defined, but
        # only if it is arithmetic over those - never arbitrary source.
        if re.fullmatch(r"[\sA-Z0-9_()+\-*/]+", val) and re.search(r"[A-Z_]", val):
            try:
                out[name] = int(eval(val, {"__builtins__": {}}, dict(out)))
            except Exception:
                pass
    return out, txt


# fixed sample state, matching what the render shows
TEMP_F, WX_CODE, IS_DAY = 84, 3, True
HH, MM, SS = 10, 9, 37
MDAY, MON, YEAR, WDAY = 1, 8, 2026, 2      # Tue 01 Sep 2026
SUNRISE, SUNSET = 7 * 60 + 4, 19 * 60 + 47


def wx_icon(c, x, y, fg, bg, mono=False):
    """Overcast: the cloud glyph the faces draw for WMO code 3."""
    col = fg if mono else 0xBDF7
    c.fillCircle(x - 6, y + 2, 5, col)
    c.fillCircle(x + 6, y + 2, 5, col)
    c.fillCircle(x, y - 2, 7, col)
    c.fillRect(x - 6, y + 2, 12, 5, col)


# --------------------------------------------------------------------------
def face_default(c):
    K, _ = consts("default")
    CX = CY = 120
    R = K["R"]
    c.fillScreen(K["C_FACE"])
    c.drawSmoothCircle(CX, CY, R, K["C_RING"], K["C_FACE"])
    for i in range(60):
        a = math.radians(i * 6)
        s, co = math.sin(a), math.cos(a)
        hour = (i % 5 == 0)
        r_out, r_in = R - 6, (R - 20 if hour else R - 12)
        x0, y0 = CX + r_in * s, CY - r_in * co
        x1, y1 = CX + r_out * s, CY - r_out * co
        if hour:
            c.drawWideLine(x0, y0, x1, y1, 3.0, K["C_TICK_HR"], K["C_FACE"])
        else:
            c.drawLine(x0, y0, x1, y1, K["C_TICK"])
    c.setTextDatum(MC)
    for n in range(1, 13):
        a = math.radians(n * 30)
        x, y = CX + int(85 * math.sin(a)), CY - int(85 * math.cos(a))
        c.setTextColor(K["C_NUM_MAJOR"] if n % 3 == 0 else K["C_NUM"], K["C_FACE"])
        c.drawNumber(n, x, y, 4)
    # weather + temp
    wx_icon(c, CX - 52, CY, K["C_TEMP"], K["C_FACE"])
    buf = str(TEMP_F)
    c.setTextColor(K["C_TEMP"], K["C_FACE"])
    tw = c.textWidth(buf, 4)
    left = CX + 48 - (tw + 11) // 2
    c.setTextDatum(ML)
    c.drawString(buf, left, CY, 4)
    c.drawCircle(left + tw + 3, CY - 7, 2, K["C_TEMP"])
    c.drawString("F", left + tw + 7, CY, 2)
    # date / digital
    c.setTextDatum(MC)
    c.setTextColor(K["C_TEXT"], K["C_FACE"])
    c.drawString("Tue 01 Sep", CX, CY - 46, 2)
    c.drawString(f"{HH:02d}:{MM:02d}:{SS:02d}", CX, CY + 50, 2)
    # hands
    s = SS
    m = MM + s / 60.0
    h = (HH % 12) + m / 60.0
    for ang, ln, back, wd, col in ((h * 30, 52, 12, 7.0, K["C_HAND"]),
                                   (m * 6, 82, 16, 5.0, K["C_HAND"]),
                                   (s * 6, 96, 26, 2.0, K["C_SEC"])):
        a = math.radians(ang)
        c.drawWideLine(CX - back * math.sin(a), CY + back * math.cos(a),
                       CX + ln * math.sin(a), CY - ln * math.cos(a),
                       wd, col, K["C_FACE"])
    a = math.radians(s * 6)
    c.fillSmoothCircle(int(CX - 26 * math.sin(a)), int(CY + 26 * math.cos(a)), 5,
                       K["C_SEC"], K["C_FACE"])
    c.fillSmoothCircle(CX, CY, 7, K["C_HUB"], K["C_FACE"])
    c.fillSmoothCircle(CX, CY, 4, 0x2E68, K["C_HUB"])


# --------------------------------------------------------------------------
def face_mosaic(c):
    K, _ = consts("mosaic")
    G, CELL, TILE = K["GRID"], K["CELL"], K["TILE"]
    OFF = K["GRID_OFF"]
    c.fillScreen(K["C_GUTTER"])
    FONT = {0: [0x0E, 0x11, 0x11, 0x11, 0x0E], 1: [0x04, 0x0C, 0x04, 0x04, 0x0E],
            2: [0x1E, 0x01, 0x0E, 0x10, 0x1F], 3: [0x1E, 0x01, 0x0E, 0x01, 0x1E],
            4: [0x12, 0x12, 0x1F, 0x02, 0x02], 5: [0x1F, 0x10, 0x1E, 0x01, 0x1E],
            6: [0x0E, 0x10, 0x1E, 0x11, 0x0E], 7: [0x1F, 0x02, 0x04, 0x08, 0x08],
            8: [0x0E, 0x11, 0x0E, 0x11, 0x0E], 9: [0x0E, 0x11, 0x0F, 0x01, 0x0E]}
    mask = [0] * (G * G)
    for d, c0, r0 in ((HH // 10, K["COL0"], K["ROWTOP"]),
                      (HH % 10, K["COL0"] + K["COLGAP"], K["ROWTOP"]),
                      (MM // 10, K["COL0"], K["ROWBOT"]),
                      (MM % 10, K["COL0"] + K["COLGAP"], K["ROWBOT"])):
        for r in range(5):
            for cc in range(5):
                if FONT[d][r] & (0x10 >> cc):
                    mask[(r0 + r) * G + c0 + cc] = 1
    # deterministic pastel field
    import random
    random.seed(7)
    for r in range(G):
        for cc in range(G):
            x, y = cc * CELL + OFF, r * CELL + OFF
            if mask[r * G + cc]:
                c.fillRect(x, y, TILE, TILE, 0x18E3)
            else:
                hue = random.randrange(256)
                v = 200 + int(26 * math.sin(random.random() * 6.28))
                c.fillRect(x, y, TILE, TILE, hsv565(hue, K["DAY_SAT"], v))


def hsv565(h, s, v):
    region = h // 43
    rem = (h - region * 43) * 6
    p = (v * (255 - s)) >> 8
    q = (v * (255 - ((s * rem) >> 8))) >> 8
    t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8
    r, g, b = [(v, t, p), (q, v, p), (p, v, t),
               (p, q, v), (t, p, v), (v, p, q)][min(region, 5)]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


# --------------------------------------------------------------------------
def face_dotmatrix(c):
    K, _ = consts("dotmatrix")
    COLS, ROWS, P, DOT = K["COLS"], K["ROWS"], K["PITCH"], K["DOT_R"]
    GX = 120 - (COLS * P) // 2 + P // 2
    GY = 120 - (ROWS * P) // 2 + P // 2
    FONT = {0: [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E],
            1: [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
            2: [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
            3: [0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E],
            4: [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02],
            5: [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
            6: [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E],
            7: [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
            8: [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E],
            9: [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C]}
    on = [0] * (COLS * ROWS)
    for d, c0, r0 in ((HH // 10, K["DL_C"], K["TOP_R"]), (HH % 10, K["DR_C"], K["TOP_R"]),
                      (MM // 10, K["DL_C"], K["BOT_R"]), (MM % 10, K["DR_C"], K["BOT_R"])):
        for r in range(7):
            for cc in range(5):
                if FONT[d][r] & (0x10 >> cc):
                    on[(r0 + r) * COLS + c0 + cc] = 1
    if SS & 1:
        on[7 * COLS + 2] = 1
        on[7 * COLS + 8] = 1
    c.fillScreen(K["C_GLASS"])
    lim = 119 - (DOT + 1)
    for r in range(ROWS):
        for cc in range(COLS):
            x, y = GX + cc * P, GY + r * P
            if (x - 120) ** 2 + (y - 120) ** 2 > lim * lim:
                continue
            if on[r * COLS + cc]:
                c.fillSmoothCircle(x, y, DOT + 1, K["C_BLOOM"], K["C_GLASS"])
                c.fillSmoothCircle(x, y, DOT, K["C_ON"], K["C_GLASS"])
            else:
                c.fillSmoothCircle(x, y, DOT - 1, K["C_OFF"], K["C_GLASS"])
    c.setTextDatum(TC)
    c.setTextColor(K["C_TEXT"], K["C_GLASS"])
    c.drawString("TUE 1", 120, 214, 2)
    c.setTextColor(K["C_TEXT"], K["C_GLASS"])
    c.drawString(str(TEMP_F), 112, 6, 4)
    w = c.textWidth(str(TEMP_F), 4)
    c.drawCircle(112 + w // 2 + 8, 11, 3, K["C_TEXT"])
    c.setTextDatum(TL)
    c.drawString("F", 112 + w // 2 + 14, 12, 2)


# --------------------------------------------------------------------------
def face_word(c, night=False):
    K, txt = consts("word")
    grid = re.search(r"GRID\[ROWS\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    rows = re.findall(r'"([^"]+)"', grid)
    spans = re.search(r"WORD\[W_COUNT\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    sp = [tuple(map(int, m)) for m in re.findall(r"\{(\d+),(\d+),(\d+)\}", spans)]
    names = ["IT", "IS", "A", "QUARTER", "TWENTY", "FIVEM", "HALF", "TENM", "TO",
             "PAST", "NINE", "ONE", "SIX", "THREE", "FOUR", "FIVE", "TWO",
             "EIGHT", "ELEVEN", "SEVEN", "TWELVE", "TEN", "OCLOCK"]
    W = dict(zip(names, sp))
    CW, CH, COLS = K["CW"], K["CH"], K["COLS"]
    GY = 120 - (K["ROWS"] * CH) // 2 + CH // 2
    HOURW = ["TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX",
             "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN"]
    slot, hr = (MM // 5) * 5, HH % 12
    lit = ["IT", "IS"]
    table = {5: ["FIVEM", "PAST"], 10: ["TENM", "PAST"], 15: ["A", "QUARTER", "PAST"],
             20: ["TWENTY", "PAST"], 25: ["TWENTY", "FIVEM", "PAST"], 30: ["HALF", "PAST"],
             35: ["TWENTY", "FIVEM", "TO"], 40: ["TWENTY", "TO"],
             45: ["A", "QUARTER", "TO"], 50: ["TENM", "TO"], 55: ["FIVEM", "TO"]}
    lit += table.get(slot, [])
    if slot >= 35:
        hr += 1
    lit.append(HOURW[hr % 12])
    if slot == 0:
        lit.append("OCLOCK")
    on = set()
    for w in lit:
        r, c0, ln = W[w]
        for i in range(ln):
            on.add((r, c0 + i))
    c.fillScreen(K["C_BG"])
    c.setTextDatum(MC)
    for r, text in enumerate(rows):
        y = GY + r * CH
        x0 = 120 - (len(text) * CW) // 2 + CW // 2
        for i, ch in enumerate(text):
            lit_here = (r, i) in on
            c.setTextColor(K["C_ON"] if lit_here else K["C_OFF"], K["C_BG"])
            c.drawString(ch, x0 + i * CW, y, 4)
            if lit_here:
                c.drawString(ch, x0 + i * CW + 1, y, 4)
    rem = MM % 5
    for i in range(4):
        a = math.radians(90 - (i - 1.5) * 30)
        x, y = 120 + int(112 * math.cos(a)), 120 + int(112 * math.sin(a))
        if i < rem:
            c.fillSmoothCircle(x, y, 4, K["C_DOT"], K["C_BG"])
        else:
            c.drawCircle(x, y, 3, K["C_OFF"])


# --------------------------------------------------------------------------
def face_pulsar(c):
    K, _ = consts("pulsar")
    P = K["P"]
    DIG_W, DIG_H = 4 * P, 6 * P
    X0, GAP, COLON_W = K["X0"], K["GAP"], K["COLON_W"]
    X1 = X0 + DIG_W + GAP
    XC = X1 + DIG_W + COLON_W // 2
    X2 = X1 + DIG_W + COLON_W
    X3 = X2 + DIG_W + GAP
    Y0 = 120 - DIG_H // 2
    SEG = {0x01: [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0)],
           0x40: [(0, 3), (1, 3), (2, 3), (3, 3), (4, 3)],
           0x08: [(0, 6), (1, 6), (2, 6), (3, 6), (4, 6)],
           0x20: [(0, 0), (0, 1), (0, 2), (0, 3)],
           0x02: [(4, 0), (4, 1), (4, 2), (4, 3)],
           0x10: [(0, 3), (0, 4), (0, 5), (0, 6)],
           0x04: [(4, 3), (4, 4), (4, 5), (4, 6)]}
    SEG_MAP = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]
    CORE = [K["C_CORE0"], K["C_CORE1"], K["C_CORE2"]]
    leds = []
    for d, x0 in ((HH // 10 if HH >= 10 else -1, X0), (HH % 10, X1),
                  (MM // 10, X2), (MM % 10, X3)):
        if d < 0:
            continue
        on = set()
        for bit, cells in SEG.items():
            if SEG_MAP[d] & bit:
                on.update(cells)
        for r in range(7):
            for cc in range(5):
                if (cc, r) in on:
                    leds.append((x0 + cc * P, Y0 + r * P, (cc * 7 + r * 13 + x0) % 3))
    if SS & 1:
        leds += [(XC, Y0 + 2 * P, 0), (XC, Y0 + 4 * P, 0)]
    c.fillScreen(K["C_BG"])
    for x, y, _ in leds:
        c.fillSmoothCircle(x, y, K["R_HAZE"], K["C_HAZE"], K["C_BG"])
    for x, y, _ in leds:
        c.fillSmoothCircle(x, y, K["R_RING"], K["C_RING"], K["C_HAZE"])
    for x, y, lvl in leds:
        c.fillSmoothCircle(x, y, K["R_CORE"], CORE[lvl], K["C_RING"])


def face_casio(c):
    faces_lcd.face_casio(c, HH, MM, SS, MDAY, WDAY, TEMP_F, night=False)


def face_casio_night(c):
    faces_lcd.face_casio(c, 21, MM, SS, MDAY, WDAY, 77, night=True)


def face_retro(c):
    faces_lcd.face_retro(c, HH, MM, SS, MDAY, MON, YEAR, WDAY, TEMP_F, SUNRISE, SUNSET)


def face_retro_night(c):
    faces_lcd.face_retro(c, 21, MM, SS, MDAY, MON, YEAR, WDAY, 77, SUNRISE, SUNSET, night=True)


FACES = {
    "default": face_default,
    "casio": face_casio,
    "casio-night": face_casio_night,
    "mosaic": face_mosaic,
    "retro": face_retro,
    "retro-night": face_retro_night,
    "dotmatrix": face_dotmatrix,
    "pulsar": face_pulsar,
    "pcb": lambda c: faces_pcb.face_pcb(c, HH, MM, SS),
    "classic": lambda c: faces_analog.face_classic(c, HH, MM, SS, MDAY),
    "outrun": lambda c: faces_analog.face_outrun(c, HH, MM, MDAY, WDAY, TEMP_F, 78),
    "outrun-day": lambda c: faces_analog.face_outrun(c, HH, MM, MDAY, WDAY, TEMP_F, 78, "DAY"),
    "outrun-dusk": lambda c: faces_analog.face_outrun(c, HH, MM, MDAY, WDAY, TEMP_F, 78, "DUSK"),
    "california": lambda c: faces_analog.face_california(c, HH, MM, SS, MDAY, WDAY,
                                                          wx_icon),
    "classic-night": lambda c: faces_analog.face_classic(c, HH, MM, SS, MDAY, "NIGHT"),
    "delorean": lambda c: faces_delorean.face_delorean(c, HH, MM, SS, MDAY, MON,
                                                       YEAR, WDAY, TEMP_F,
                                                       SUNRISE, SUNSET),
    "panel": lambda c: faces_panel.face_panel(c, HH, MM, SS, MDAY, MON, WDAY,
                                              TEMP_F, SUNRISE, SUNSET, night=False),
    "panel-night": lambda c: faces_panel.face_panel(c, HH, MM, SS, MDAY, MON, WDAY,
                                                    TEMP_F, SUNRISE, SUNSET, night=True),
    "modern": lambda c: faces_analog.face_modern(c, HH, MM, SS, MDAY, WDAY,
                                                 TEMP_F, IS_DAY, SUNSET),
    "word": face_word,
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(HERE, "..", "..", "docs", "faces"))
    os.makedirs(out, exist_ok=True)
    for name, fn in FACES.items():
        c = Canvas()
        fn(c)
        p = os.path.join(out, f"{name}.png")
        c.save(p)
        print(f"  {name:10} -> {p}")


if __name__ == "__main__":
    main()
