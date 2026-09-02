"""A TFT_eSPI-compatible canvas, for rendering the watch faces off-device.

This reimplements the drawing primitives the faces use, matching TFT_eSPI's
semantics (datum handling, anti-aliased circles and wide lines, the two font
decode paths). It is a reimplementation, not the firmware's own code, so it
can in principle drift - but the glyphs come from the library's real font
tables and the geometry comes from the faces' own constants, so what is left
to drift is only the primitive rasterisation.
"""
import math
from PIL import Image
import fonts

TL, TC, TR, ML, MC, MR, BL, BC, BR = range(9)

_F2 = fonts.font16()
_F4 = fonts.font32()
_F1 = fonts.glcd()


def rgb565(c):
    return (((c >> 11) & 0x1F) * 255 // 31,
            ((c >> 5) & 0x3F) * 255 // 63,
            (c & 0x1F) * 255 // 31)


class Canvas:
    W = H = 240

    def __init__(self):
        self.px = [0] * (self.W * self.H)
        self.fg, self.bg = 0xFFFF, 0x0000
        self.datum = TL
        self.tsize = 1

    # ---- state ----
    def setTextColor(self, c, b=None):
        self.fg = c
        if b is not None:
            self.bg = b

    def setTextDatum(self, d):
        self.datum = d

    def setTextSize(self, s):
        self.tsize = max(1, int(s))

    # ---- pixels ----
    def drawPixel(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.W and 0 <= y < self.H:
            self.px[y * self.W + x] = c

    def _blend(self, c, b, a):
        a = max(0.0, min(1.0, a))
        cr, cg, cb = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
        br, bgr, bb = (b >> 11) & 0x1F, (b >> 5) & 0x3F, b & 0x1F
        return ((round(br + (cr - br) * a) << 11)
                | (round(bgr + (cg - bgr) * a) << 5)
                | round(bb + (cb - bb) * a))

    def fillScreen(self, c):
        self.px = [c] * (self.W * self.H)

    fillSprite = fillScreen

    def fillRect(self, x, y, w, h, c):
        for j in range(int(h)):
            for i in range(int(w)):
                self.drawPixel(x + i, y + j, c)

    def drawRect(self, x, y, w, h, c):
        self.drawFastHLine(x, y, w, c)
        self.drawFastHLine(x, y + h - 1, w, c)
        self.drawFastVLine(x, y, h, c)
        self.drawFastVLine(x + w - 1, y, h, c)

    def drawFastHLine(self, x, y, w, c):
        for i in range(int(w)):
            self.drawPixel(x + i, y, c)

    def drawFastVLine(self, x, y, h, c):
        for i in range(int(h)):
            self.drawPixel(x, y + i, c)

    def drawLine(self, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.drawPixel(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def fillCircle(self, cx, cy, r, c):
        r = int(r)
        for y in range(-r, r + 1):
            for x in range(-r, r + 1):
                if x * x + y * y <= r * r:
                    self.drawPixel(cx + x, cy + y, c)

    def drawCircle(self, cx, cy, r, c):
        for a in range(0, 720):
            t = math.radians(a * 0.5)
            self.drawPixel(cx + round(r * math.cos(t)), cy + round(r * math.sin(t)), c)

    def fillSmoothCircle(self, cx, cy, r, c, bgc=0x0000):
        r = int(r)
        for y in range(-r - 1, r + 2):
            for x in range(-r - 1, r + 2):
                d = math.hypot(x, y)
                cov = min(1.0, max(0.0, r + 0.5 - d))
                if cov > 0:
                    self.drawPixel(int(cx + x), int(cy + y), self._blend(c, bgc, cov))

    def drawSmoothCircle(self, cx, cy, r, c, bgc=0x0000):
        r = int(r)
        for y in range(-r - 2, r + 3):
            for x in range(-r - 2, r + 3):
                d = math.hypot(x, y)
                cov = min(1.0, max(0.0, 1.0 - abs(d - r)))
                if cov > 0:
                    self.drawPixel(int(cx + x), int(cy + y), self._blend(c, bgc, cov))

    def fillRoundRect(self, x, y, w, h, r, c):
        x, y, w, h, r = int(x), int(y), int(w), int(h), int(r)
        self.fillRect(x + r, y, w - 2 * r, h, c)
        self.fillRect(x, y + r, r, h - 2 * r, c)
        self.fillRect(x + w - r, y + r, r, h - 2 * r, c)
        for cx, cy in ((x + r, y + r), (x + w - r - 1, y + r),
                       (x + r, y + h - r - 1), (x + w - r - 1, y + h - r - 1)):
            self.fillCircle(cx, cy, r, c)

    def drawRoundRect(self, x, y, w, h, r, c):
        self.drawFastHLine(x + r, y, w - 2 * r, c)
        self.drawFastHLine(x + r, y + h - 1, w - 2 * r, c)
        self.drawFastVLine(x, y + r, h - 2 * r, c)
        self.drawFastVLine(x + w - 1, y + r, h - 2 * r, c)

    def fillTriangle(self, x0, y0, x1, y1, x2, y2, c):
        xs, ys = [int(x0), int(x1), int(x2)], [int(y0), int(y1), int(y2)]

        def sign(ax, ay, bx, by, cx, cy):
            return (ax - cx) * (by - cy) - (bx - cx) * (ay - cy)

        for y in range(min(ys), max(ys) + 1):
            for x in range(min(xs), max(xs) + 1):
                d1 = sign(x, y, xs[0], ys[0], xs[1], ys[1])
                d2 = sign(x, y, xs[1], ys[1], xs[2], ys[2])
                d3 = sign(x, y, xs[2], ys[2], xs[0], ys[0])
                if not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0)):
                    self.drawPixel(x, y, c)

    def drawWideLine(self, ax, ay, bx, by, wd, c, bgc=0x0000):
        dx, dy = bx - ax, by - ay
        ln = math.hypot(dx, dy)
        r = wd / 2.0
        if ln < 0.001:
            self.fillSmoothCircle(int(ax), int(ay), int(r), c, bgc)
            return
        for y in range(int(min(ay, by) - r - 1), int(max(ay, by) + r + 2)):
            for x in range(int(min(ax, bx) - r - 1), int(max(ax, bx) + r + 2)):
                t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (ln * ln)))
                d = math.hypot(ax + t * dx - x, ay + t * dy - y)
                cov = min(1.0, max(0.0, r + 0.5 - d))
                if cov > 0:
                    self.drawPixel(x, y, self._blend(c, bgc, cov))

    # ---- text ----
    def _fontinfo(self, f):
        return _F2 if f == 2 else _F4

    def textWidth(self, s, f=2):
        if f == 1:
            return 6 * self.tsize * len(s)
        fi = self._fontinfo(f)
        return sum(fi["widths"][ord(ch) - 32] for ch in s
                   if 32 <= ord(ch) <= 127) * self.tsize

    def fontHeight(self, f=2):
        if f == 1:
            return 8 * self.tsize
        return self._fontinfo(f)["height"] * self.tsize

    def drawString(self, s, x, y, f=2):
        s = str(s)
        w, h = self.textWidth(s, f), self.fontHeight(f)
        if self.datum in (TC, MC, BC):
            x -= w // 2
        elif self.datum in (TR, MR, BR):
            x -= w
        if self.datum in (ML, MC, MR):
            y -= h // 2
        elif self.datum in (BL, BC, BR):
            y -= h
        for ch in s:
            x += self._drawChar(ord(ch), int(x), int(y), f)

    def drawNumber(self, n, x, y, f=2):
        self.drawString(str(int(n)), x, y, f)

    def _drawChar(self, u, x, y, f):
        if not (32 <= u <= 127):
            return 0
        ts = self.tsize

        if f == 1:                                   # GLCD 5x7, column-major
            for col in range(5):
                line = _F1[u * 5 + col]
                for row in range(8):
                    if line & 1:
                        if ts == 1:
                            self.drawPixel(x + col, y + row, self.fg)
                        else:
                            self.fillRect(x + col * ts, y + row * ts, ts, ts, self.fg)
                    line >>= 1
            return 6 * ts

        fi = self._fontinfo(f)
        idx = u - 32
        width, height = fi["widths"][idx], fi["height"]
        data = fi["chars"][idx]

        if f == 2:                                   # plain bitmap, w bytes/row
            w = (width + 6) // 8
            for i in range(height):
                for k in range(w):
                    if w * i + k >= len(data):
                        continue
                    line = data[w * i + k]
                    for b in range(8):
                        if line & (0x80 >> b):
                            if ts == 1:
                                self.drawPixel(x + k * 8 + b, y + i, self.fg)
                            else:
                                self.fillRect(x + (k * 8 + b) * ts,
                                              y + i * ts, ts, ts, self.fg)
            return width * ts

        # font 4: RLE. High bit set means a run of lit pixels.
        total, pc, p = width * height, 0, 0
        while pc < total and p < len(data):
            line = data[p]
            p += 1
            if line & 0x80:
                line = (line & 0x7F) + 1
                for _ in range(line):
                    px_, py_ = x + ts * (pc % width), y + ts * (pc // width)
                    if ts == 1:
                        self.drawPixel(px_, py_, self.fg)
                    else:
                        self.fillRect(px_, py_, ts, ts, self.fg)
                    pc += 1
            else:
                pc += line + 1
        return width * ts

    # ---- output ----
    def save(self, path, scale=2, round_mask=True):
        img = Image.new("RGB", (self.W, self.H))
        out = img.load()
        cx = cy = self.W / 2.0 - 0.5
        r = self.W / 2.0
        for y in range(self.H):
            for x in range(self.W):
                if round_mask and math.hypot(x - cx, y - cy) > r:
                    out[x, y] = (18, 18, 20)          # outside the bezel
                else:
                    out[x, y] = rgb565(self.px[y * self.W + x])
        if scale != 1:
            img = img.resize((self.W * scale, self.H * scale), Image.NEAREST)
        img.save(path)
