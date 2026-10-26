#!/usr/bin/env python3
"""Generates the ribbon / Quick Access icons of Folio Gable in the Folio suite style:
thin charcoal outlines, white bodies, the app's purple as the accent tint.
Each icon is drawn once on a 32-unit grid; the "-sm" variant (rendered at 16 px) gets
doubled stroke widths so lines stay one device pixel wide.

usage: make_icons.py OUTDIR
"""
import math
import os
import sys

INK = "#3b3b3b"
GRAY = "#9a9a9a"
LIGHTGRAY = "#d4d4d4"
WHITE = "#ffffff"
TINT = "#e6dcf3"      # accent tint fill
TINT2 = "#c9b4e6"     # deeper tint (shaded side)
ACC = "#7a4cc0"       # accent
ACCD = "#4f2a85"      # accent dark (accent strokes)
RED = "#d9342b"
GREEN = "#2e9a45"
BLUE = "#2f5fd0"
GOLD = "#e0a526"


class Icon:
    def __init__(self, sw):
        self.sw = sw          # base stroke width
        self.parts = []

    def _s(self, stroke, width):
        if stroke is None:
            return 'stroke="none"'
        w = self.sw * (width if width is not None else 1)
        return f'stroke="{stroke}" stroke-width="{w:g}" stroke-linejoin="round" stroke-linecap="round"'

    def rect(self, x, y, w, h, fill=WHITE, stroke=INK, rx=0, width=None, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="{rx:g}" fill="{fill}" {self._s(stroke, width)}{d}/>')

    def poly(self, pts, fill=WHITE, stroke=INK, width=None, closed=True, dash=None):
        tag = "polygon" if closed else "polyline"
        p = " ".join(f"{x:g},{y:g}" for x, y in pts)
        d = f' stroke-dasharray="{dash}"' if dash else ""
        if not closed:
            fill = "none"
        self.parts.append(f'<{tag} points="{p}" fill="{fill}" {self._s(stroke, width)}{d}/>')

    def line(self, x1, y1, x2, y2, stroke=INK, width=None, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<line x1="{x1:g}" y1="{y1:g}" x2="{x2:g}" y2="{y2:g}" {self._s(stroke, width)}{d}/>')

    def circle(self, cx, cy, r, fill=WHITE, stroke=INK, width=None, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<circle cx="{cx:g}" cy="{cy:g}" r="{r:g}" fill="{fill}" {self._s(stroke, width)}{d}/>')

    def path(self, d, fill="none", stroke=INK, width=None, dash=None):
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<path d="{d}" fill="{fill}" {self._s(stroke, width)}{da}/>')

    def text(self, x, y, s, size=12, fill=INK, weight="bold", anchor="middle", family="sans-serif"):
        self.parts.append(f'<text x="{x:g}" y="{y:g}" font-family="{family}" font-size="{size:g}" '
                          f'font-weight="{weight}" text-anchor="{anchor}" fill="{fill}">{s}</text>')

    def dot(self, x, y, r=1.8, fill=ACC):
        self.circle(x, y, r, fill=fill, stroke=None)

    def svg(self):
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">'
                + "".join(self.parts) + "</svg>\n")


# ---------------------------------------------------------------- shared pieces
def box(ic, x, y, w, h, d, top=WHITE, front=TINT, side=TINT2, stroke=INK):
    """Oblique box: front rectangle at (x,y) w*h, depth d up-right."""
    ic.poly([(x, y), (x + d, y - d), (x + w + d, y - d), (x + w, y)], fill=top, stroke=stroke)
    ic.poly([(x + w, y), (x + w + d, y - d), (x + w + d, y + h - d), (x + w, y + h)], fill=side, stroke=stroke)
    ic.rect(x, y, w, h, fill=front, stroke=stroke)


def house(ic, ox=0, oy=0, s=1.0, fill_front=WHITE, fill_side=TINT, fill_roof=TINT2):
    P = lambda x, y: (ox + x * s, oy + y * s)
    ic.poly([P(5, 16), P(15, 21), P(15, 29), P(5, 24)], fill=fill_front)
    ic.poly([P(15, 21), P(27, 15), P(27, 23), P(15, 29)], fill=fill_side)
    ic.poly([P(5, 16), P(10, 7), P(22, 3), P(27, 15), P(15, 21)], fill=fill_roof)
    ic.line(*P(10, 7), *P(15, 21))


def arrow(ic, x1, y1, x2, y2, color=ACCD, width=1.6, head=4):
    ic.line(x1, y1, x2, y2, stroke=color, width=width)
    a = math.atan2(y2 - y1, x2 - x1)
    for s in (-1, 1):
        ic.line(x2, y2, x2 - head * math.cos(a + s * 0.5), y2 - head * math.sin(a + s * 0.5), stroke=color, width=width)


def pencil(ic, x, y):
    # small pencil, tip at (x, y), pointing down-left
    ic.poly([(x, y), (x + 2, y - 5), (x + 9, y - 12), (x + 12, y - 9), (x + 5, y - 2)], fill=GOLD)
    ic.poly([(x, y), (x + 2, y - 5), (x + 5, y - 2)], fill=WHITE)
    ic.line(x + 9, y - 12, x + 12, y - 9)


def magnifier(ic, cx, cy, r=6, handle=True):
    ic.circle(cx, cy, r, fill=WHITE, width=1.6)
    if handle:
        ic.line(cx + r * 0.7, cy + r * 0.7, cx + r * 0.7 + 6, cy + r * 0.7 + 6, width=3)


def eye(ic, cx, cy, w=12, closed=False):
    ic.path(f"M{cx-w} {cy} Q{cx} {cy-8} {cx+w} {cy} Q{cx} {cy+8} {cx-w} {cy}Z", fill=WHITE)
    ic.circle(cx, cy, 3.2, fill=ACC, stroke=ACCD)
    if closed:
        ic.line(cx - w + 2, cy + 7, cx + w - 2, cy - 7, stroke=RED, width=1.8)


def axes_glyph(ic, ox, oy, L=10):
    ic.line(ox, oy, ox + L, oy + L * 0.45, stroke=RED, width=1.6)
    ic.line(ox, oy, ox - L * 0.8, oy + L * 0.5, stroke=GREEN, width=1.6)
    ic.line(ox, oy, ox, oy - L, stroke=BLUE, width=1.6)


def camera(ic, x, y):
    ic.rect(x, y, 15, 10, fill=TINT, rx=1.5)
    ic.poly([(x + 15, y + 3), (x + 21, y), (x + 21, y + 10), (x + 15, y + 7)], fill=TINT2)


def faces_cube(ic, mode):
    """The display-style cube used by the face style buttons."""
    top = [(6, 11), (16, 6), (26, 11), (16, 16)]
    left = [(6, 11), (16, 16), (16, 27), (6, 22)]
    right = [(16, 16), (26, 11), (26, 22), (16, 27)]
    if mode == "wire":
        for p in (top, left, right):
            ic.poly(p, fill="none")
        ic.poly([(6, 22), (16, 17), (26, 22)], closed=False, stroke=GRAY, dash="2 1.5")
        ic.line(16, 6, 16, 17, stroke=GRAY, dash="2 1.5")
    elif mode == "hidden":
        for p in (top, left, right):
            ic.poly(p, fill=WHITE)
    elif mode == "shaded":
        ic.poly(top, fill=WHITE); ic.poly(left, fill=TINT); ic.poly(right, fill=TINT2)
    elif mode == "textured":
        ic.poly(top, fill="#f2e3c6"); ic.poly(left, fill="#d9b98a"); ic.poly(right, fill="#c09a66")
        for k in range(3):
            ic.line(6, 14 + k * 3.5, 16, 19 + k * 3.5, stroke="#9a7444", width=0.7)
            ic.line(16, 19 + k * 3.5, 26, 14 + k * 3.5, stroke="#8a6434", width=0.7)
    elif mode == "mono":
        ic.poly(top, fill=WHITE); ic.poly(left, fill="#e3e3e3"); ic.poly(right, fill="#c4c4c4")
    elif mode == "xray":
        for p, f in ((top, "#f3eefa"), (left, "#e6dcf3"), (right, "#d8c9ee")):
            ic.poly(p, fill=f)
        ic.poly([(6, 22), (16, 17), (26, 22)], closed=False, stroke=INK)
        ic.line(16, 6, 16, 17, stroke=INK)
    elif mode == "backedges":
        ic.poly(top, fill=WHITE); ic.poly(left, fill=TINT); ic.poly(right, fill=TINT2)
        ic.poly([(6, 22), (16, 17), (26, 22)], closed=False, stroke=INK, dash="2 1.5")
        ic.line(16, 6, 16, 17, stroke=INK, dash="2 1.5")


def view_cube(ic, face):
    """Standard view: a cube with the viewed face highlighted."""
    top = [(6, 11), (16, 6), (26, 11), (16, 16)]
    left = [(6, 11), (16, 16), (16, 27), (6, 22)]
    right = [(16, 16), (26, 11), (26, 22), (16, 27)]
    hl = {"top": top, "front": left, "right": right}
    for name, p in (("top", top), ("front", left), ("right", right)):
        ic.poly(p, fill=ACC if hl.get(face) is p else WHITE)
    if face == "iso":
        ic.poly(top, fill=TINT); ic.poly(left, fill=TINT); ic.poly(right, fill=TINT)
    if face in ("back", "left", "bottom"):
        hidden = {"back": [(16, 6), (26, 11), (26, 22), (16, 17)],
                  "left": [(6, 11), (16, 6), (16, 17), (6, 22)],
                  "bottom": [(6, 22), (16, 17), (26, 22), (16, 27)]}[face]
        ic.poly(hidden, fill="none", stroke=ACCD, dash="2 1.5", width=1.4)


# ---------------------------------------------------------------- icons
ICONS = {}


def icon(name):
    def deco(fn):
        ICONS[name] = fn
        return fn
    return deco


@icon("select")
def _(ic):
    ic.path("M9 4 L9 25 L14 20 L18 28.5 L21.5 27 L17.5 18.5 L24 18.5 Z", fill=WHITE, width=1.3)


@icon("eraser")
def _(ic):
    ic.poly([(4, 21), (15, 10), (25, 20), (18, 27), (10, 27)], fill=WHITE)
    ic.poly([(15, 10), (20, 5), (30, 15), (25, 20)], fill="#f3a6b5")
    ic.line(8, 29, 28, 29, stroke=GRAY)


@icon("line")
def _(ic):
    ic.line(6, 26, 25, 7, width=1.8)
    pencil(ic, 18, 15)
    ic.dot(6, 26, 2.2)


@icon("freehand")
def _(ic):
    ic.path("M3 23 C8 8 12 28 17 16 C20 9 23 14 25 11", width=1.6)
    pencil(ic, 18, 15)


@icon("rect")
def _(ic):
    ic.rect(4.5, 9.5, 23, 15, fill=TINT)
    ic.dot(4.5, 24.5, 2.2)
    ic.dot(27.5, 9.5, 2.2)


@icon("rotrect")
def _(ic):
    ic.poly([(3, 18), (17, 7), (28, 17), (14, 28)], fill=TINT)
    ic.path("M21 22 A8 8 0 0 0 23 12", stroke=ACCD, width=1.2)
    ic.dot(3, 18, 2); ic.dot(17, 7, 2)


@icon("circle")
def _(ic):
    ic.circle(16, 16, 11, fill=TINT)
    ic.line(16, 16, 27, 16, stroke=ACCD, dash="2 1.5")
    ic.dot(16, 16, 2)


@icon("polygon")
def _(ic):
    pts = [(16 + 11 * math.cos(a), 16 + 11 * math.sin(a)) for a in [math.pi / 6 + k * math.pi / 3 for k in range(6)]]
    ic.poly(pts, fill=TINT)
    ic.dot(16, 16, 2)


@icon("arc")
def _(ic):
    ic.path("M5 24 A11 11 0 0 1 27 24", width=1.8)
    ic.line(16, 24, 5, 24, stroke=ACCD, dash="2 1.5")
    ic.line(16, 24, 26, 17, stroke=ACCD, dash="2 1.5")
    ic.dot(16, 24, 2)


@icon("arc2")
def _(ic):
    ic.path("M4 24 A12 10 0 0 1 28 24", width=1.8)
    ic.line(16, 24, 16, 14, stroke=ACCD, dash="2 1.5")
    ic.dot(4, 24, 2.2); ic.dot(28, 24, 2.2)


@icon("arc3")
def _(ic):
    ic.path("M4 25 Q9 6 27 12", width=1.8)
    ic.dot(4, 25, 2.2); ic.dot(11.5, 12.5, 2.2); ic.dot(27, 12, 2.2)


@icon("pie")
def _(ic):
    ic.path("M16 17 L5 17 A11 11 0 0 1 24 9 Z", fill=TINT)
    ic.dot(16, 17, 2)


@icon("pushpull")
def _(ic):
    box(ic, 5, 16, 15, 11, 6)
    ic.poly([(5, 16), (11, 10), (26, 10), (20, 16)], fill=TINT2, stroke=ACCD)
    arrow(ic, 15, 13, 15, 2, head=4)


@icon("followme")
def _(ic):
    ic.path("M4 26 L4 12 Q4 6 10 6 L28 6", stroke=GRAY, width=1.2, dash="2.5 1.5")
    ic.path("M7 28 L7 14 Q7 10 11 10 L28 10 L28 15 L12 15 Q11 15 11 16 L11 28 Z", fill=TINT)
    arrow(ic, 18, 22, 27, 22, head=3.5)


@icon("move")
def _(ic):
    for (x1, y1, x2, y2) in ((16, 16, 16, 3), (16, 16, 16, 29), (16, 16, 3, 16), (16, 16, 29, 16)):
        arrow(ic, x1, y1, x2, y2, color=RED, width=1.8, head=4.5)
    ic.circle(16, 16, 2.6, fill=WHITE, stroke=RED, width=1.4)


@icon("rotate")
def _(ic):
    ic.path("M26 16 A10 10 0 1 1 20 6.8", stroke=RED, width=2)
    ic.poly([(18.5, 2.5), (24.5, 7), (18, 10)], fill=RED, stroke=RED, width=0.8)
    ic.circle(16, 16, 2.2, fill=WHITE, stroke=INK)


@icon("scale")
def _(ic):
    ic.rect(4.5, 12.5, 15, 15, fill=TINT)
    ic.rect(4.5, 4.5, 23, 23, fill="none", stroke=GRAY, dash="2 1.5")
    for x, y in ((4.5, 4.5), (27.5, 4.5), (27.5, 27.5), (4.5, 27.5), (16, 4.5), (27.5, 16)):
        ic.rect(x - 2, y - 2, 4, 4, fill=GREEN, stroke=INK, width=0.8)
    arrow(ic, 16, 16, 24, 8, color=RED, width=1.4, head=3)


@icon("offset")
def _(ic):
    ic.rect(4.5, 4.5, 23, 23, fill=TINT)
    ic.rect(10.5, 10.5, 11, 11, fill=WHITE, stroke=ACCD, width=1.3)
    arrow(ic, 7, 16, 12.5, 16, color=RED, width=1.2, head=2.5)


@icon("paint")
def _(ic):
    ic.poly([(6, 14), (15, 5), (25, 15), (16, 24)], fill=WHITE)
    ic.path("M6 14 Q2 20 5 24 Q8 26 9 22 L8 16", fill=ACC, stroke=ACCD)
    ic.line(15, 5, 12, 2, width=1.4)
    ic.path("M15 5 Q20 1 24 4", width=1.2)


@icon("tape")
def _(ic):
    ic.circle(12, 17, 9, fill=GOLD)
    ic.circle(12, 17, 3, fill=WHITE)
    ic.poly([(12, 26), (29, 26), (29, 21), (17, 21)], fill="#f7e2a2")
    for x in (20, 23, 26):
        ic.line(x, 26, x, 23.5, width=0.8)


@icon("protractor")
def _(ic):
    ic.path("M3 26 A13 13 0 0 1 29 26 Z", fill=GOLD)
    ic.path("M9 26 A7 7 0 0 1 23 26", fill=WHITE)
    for a in range(15, 180, 30):
        r = math.radians(a)
        ic.line(16 + 13 * math.cos(r), 26 - 13 * math.sin(r), 16 + 10.5 * math.cos(r), 26 - 10.5 * math.sin(r), width=0.8)


@icon("dimension")
def _(ic):
    ic.line(4, 8, 4, 26, stroke=GRAY); ic.line(28, 8, 28, 26, stroke=GRAY)
    ic.line(4, 20, 28, 20, width=1.2)
    ic.poly([(4, 20), (9, 17.5), (9, 22.5)], fill=INK, width=0.6)
    ic.poly([(28, 20), (23, 17.5), (23, 22.5)], fill=INK, width=0.6)
    ic.text(16, 15, "1.2", size=8.5, fill=ACCD)


@icon("text")
def _(ic):
    ic.rect(10.5, 4.5, 19, 12, fill=WHITE, rx=1)
    ic.text(20, 13.5, "A", size=9, fill=ACCD)
    ic.line(11, 16, 3, 27, width=1.2)
    ic.dot(3, 27, 2, fill=INK)


@icon("text3d")
def _(ic):
    ic.path("M7 26 L14 5 L18 5 L25 26 L20.5 26 L19 21 L13 21 L11.5 26 Z", fill=TINT2, stroke=ACCD)
    ic.path("M5 24 L12 3 L16 3 L23 24 L18.5 24 L17 19 L11 19 L9.5 24 Z M12 15 L16 15 L14 8 Z", fill=WHITE)


@icon("axes")
def _(ic):
    axes_glyph(ic, 14, 20, 12)
    ic.dot(14, 20, 2, fill=INK)


@icon("section")
def _(ic):
    ic.poly([(4, 10), (20, 4), (28, 10), (12, 16)], fill="#ffd9a8", stroke="#c06a00")
    box(ic, 8, 18, 14, 10, 5)
    ic.poly([(8, 18), (13, 13), (27, 13), (22, 18)], fill="#ffb366", stroke="#c06a00")
    for (x, y) in ((24, 8), (8, 12)):
        arrow(ic, x, y, x, y - 0.1, color="#c06a00", width=1, head=0.1)


@icon("orbit")
def _(ic):
    ic.path("M5 18 A11 6 0 1 0 16 12", stroke=ACCD, width=1.8)
    ic.poly([(13, 8.5), (18, 12), (13, 15)], fill=ACCD, stroke=ACCD, width=0.8)
    ic.circle(16, 18, 4.5, fill=TINT)


@icon("pan")
def _(ic):
    ic.path("M10 28 L7 18 Q6 15 8.5 15.5 L11 20 L11 7 Q11 5 13 5 Q15 5 15 7 L15 15 L15 5 Q15 3 17 3 Q19 3 19 5 L19 15 "
            "L19 6 Q19 4 21 4 Q23 4 23 6 L23 16 L23 10 Q23 8 25 8 Q27 8 27 10 L27 20 Q27 26 22 28 Z", fill=WHITE, width=1.1)


@icon("zoom")
def _(ic):
    magnifier(ic, 13, 13, 8)
    ic.line(13, 9, 13, 17, stroke=ACCD, width=1.6); ic.line(9, 13, 17, 13, stroke=ACCD, width=1.6)


@icon("search")
def _(ic):
    ic.circle(13, 13, 8, fill="none", width=2)
    ic.line(19, 19, 27, 27, width=2.6)


@icon("zoomwindow")
def _(ic):
    ic.rect(3.5, 3.5, 15, 12, fill="none", stroke=ACCD, dash="2 1.5")
    magnifier(ic, 17, 17, 6.5)


@icon("zoomextents")
def _(ic):
    magnifier(ic, 14, 14, 8)
    for (x, y, dx, dy) in ((3, 3, 1, 1), (29, 3, -1, 1), (3, 29, 1, -1)):
        ic.poly([(x + 5 * dx, y), (x, y), (x, y + 5 * dy)], closed=False, stroke=ACCD, width=1.5)
    ic.rect(10.5, 10.5, 7, 7, fill=TINT, stroke=ACCD, width=0.9)


@icon("zoomsel")
def _(ic):
    magnifier(ic, 14, 14, 8)
    ic.rect(10.5, 10.5, 7, 7, fill=ACC, stroke=ACCD, width=0.9)


@icon("previous")
def _(ic):
    camera(ic, 9, 12)
    ic.path("M10 9 Q4 9 4 15", stroke=ACCD, width=1.6)
    ic.poly([(1.5, 13), (4, 17.5), (6.5, 13)], fill=ACCD, stroke=ACCD, width=0.8)


@icon("poscam")
def _(ic):
    ic.circle(16, 9, 4, fill=TINT)
    ic.path("M9 28 L11 16 Q16 13 21 16 L23 28 Z", fill=WHITE)
    ic.poly([(3, 29), (29, 29)], closed=False, stroke=GRAY)


@icon("walk")
def _(ic):
    ic.path("M10 27 Q8 22 9 17 Q10 12 13 12 Q16 12 16 17 Q16 22 14 27 Z", fill=WHITE)
    ic.path("M18 20 Q16 15 17 10 Q18 5 21 5 Q24 5 24 10 Q24 15 22 20 Z", fill=TINT)


@icon("lookaround")
def _(ic):
    eye(ic, 16, 18, 12)
    ic.path("M7 8 Q16 2 25 8", stroke=ACCD, width=1.5)
    ic.poly([(22, 5), (26.5, 8.5), (21.5, 10)], fill=ACCD, stroke=ACCD, width=0.8)


def _terrain(ic, fill=TINT, grid=True):
    ic.path("M3 22 Q9 12 15 17 Q21 22 29 13 L29 25 L3 25 Z", fill=fill)
    if grid:
        for x in (9, 15, 21):
            ic.line(x, 25, x, 15.5 if x != 15 else 17, stroke=ACCD, width=0.7)


@icon("sbcontours")
def _(ic):
    for k, (r, f) in enumerate(((12, "#efe8f8"), (8, TINT), (4, TINT2))):
        ic.path(f"M{16-r*1.2} 18 Q16 {18-r*1.1} {16+r*1.2} 18 Q16 {18+r*0.6} {16-r*1.2} 18 Z", fill=f)


@icon("sbscratch")
def _(ic):
    ic.poly([(3, 20), (14, 13), (29, 18), (18, 26)], fill=TINT)
    for t in (1 / 3, 2 / 3):
        ic.line(3 + 11 * t, 20 - 7 * t, 18 + 11 * t, 26 - 8 * t, stroke=ACCD, width=0.7)
        ic.line(3 + 15 * t, 20 + 6 * t, 14 + 15 * t, 13 + 5 * t, stroke=ACCD, width=0.7)
    pencil(ic, 18, 13)


@icon("smoove")
def _(ic):
    _terrain(ic)
    arrow(ic, 15, 14, 15, 3, head=3.5)


@icon("drape")
def _(ic):
    _terrain(ic, grid=False)
    ic.rect(10.5, 3.5, 11, 6, fill=WHITE, stroke=ACCD)
    for x in (12, 20):
        arrow(ic, x, 10, x, 15, color=ACCD, width=1, head=2)


@icon("sbdetail")
def _(ic):
    ic.rect(4.5, 4.5, 23, 23, fill=TINT)
    ic.line(16, 4.5, 16, 27.5); ic.line(4.5, 16, 27.5, 16)
    ic.line(4.5, 4.5, 27.5, 27.5, stroke=ACCD, width=0.8); ic.line(27.5, 4.5, 4.5, 27.5, stroke=ACCD, width=0.8)
    ic.line(10, 4.5, 10, 16, stroke=ACCD, width=0.8); ic.line(4.5, 10, 16, 10, stroke=ACCD, width=0.8)


@icon("flipedge")
def _(ic):
    ic.poly([(4, 16), (16, 4), (28, 16), (16, 28)], fill=TINT)
    ic.line(4, 16, 28, 16, stroke=GRAY, dash="2 1.5")
    ic.line(16, 4, 16, 28, stroke=ACCD, width=1.8)


# ---- clipboard & edit
@icon("paste")
def _(ic):
    ic.rect(5.5, 5.5, 17, 22, fill=TINT, stroke=ACCD, rx=1.5)
    ic.rect(10.5, 3.5, 7, 4, fill=ACC, stroke=ACCD, rx=1)
    ic.path("M12.5 11.5h9l5 5v13h-14z", fill=WHITE)
    ic.path("M21.5 11.5v5h5", fill="none")


@icon("pasteinplace")
def _(ic):
    ic.rect(5.5, 5.5, 17, 22, fill=TINT, stroke=ACCD, rx=1.5)
    ic.rect(10.5, 3.5, 7, 4, fill=ACC, stroke=ACCD, rx=1)
    ic.rect(12.5, 12.5, 14, 16, fill=WHITE)
    ic.circle(19.5, 20.5, 3, fill="none", stroke=RED, width=1.2)
    ic.line(19.5, 16, 19.5, 25, stroke=RED, width=0.8); ic.line(15, 20.5, 24, 20.5, stroke=RED, width=0.8)


@icon("cut")
def _(ic):
    ic.circle(9, 24, 4, fill=WHITE, width=1.4); ic.circle(23, 24, 4, fill=WHITE, width=1.4)
    ic.line(11.5, 21, 22, 4, width=1.6); ic.line(20.5, 21, 10, 4, width=1.6)


@icon("copy")
def _(ic):
    ic.path("M5.5 4.5h10l4 4v14h-14z", fill=WHITE)
    ic.path("M12.5 9.5h10l4 4v14h-14z", fill=TINT)
    ic.path("M22.5 9.5v4h4", fill="none")


@icon("delete")
def _(ic):
    ic.line(8, 8, 24, 24, stroke=RED, width=3); ic.line(24, 8, 8, 24, stroke=RED, width=3)


@icon("selectall")
def _(ic):
    ic.rect(4.5, 4.5, 23, 23, fill="none", stroke=ACCD, dash="2.5 2")
    box(ic, 9, 15, 10, 9, 4)


@icon("selectnone")
def _(ic):
    ic.rect(4.5, 4.5, 23, 23, fill="none", stroke=GRAY, dash="2.5 2")
    box(ic, 9, 15, 10, 9, 4, front=WHITE, side="#e8e8e8")
    ic.line(5, 27, 27, 5, stroke=RED, width=1.6)


@icon("invertsel")
def _(ic):
    ic.rect(4.5, 4.5, 23, 23, fill=TINT, stroke=ACCD, dash="2.5 2")
    ic.rect(11.5, 11.5, 9, 9, fill=WHITE)


@icon("makegroup")
def _(ic):
    ic.rect(3.5, 5.5, 25, 22, fill="none", stroke=ACCD, dash="3 2")
    box(ic, 8, 15, 12, 9, 4)


@icon("makecomponent")
def _(ic):
    for (x, y) in ((4, 14), (16, 8)):
        box(ic, x, y + 6, 9, 8, 4)
    ic.rect(2.5, 3.5, 27, 25, fill="none", stroke=ACC, dash="3 2")


@icon("closegroup")
def _(ic):
    ic.rect(3.5, 5.5, 25, 22, fill=WHITE, stroke=ACCD, dash="3 2")
    arrow(ic, 22, 16, 9, 16, color=ACCD, head=4)


@icon("explode")
def _(ic):
    ic.poly([(16, 3), (19, 11), (28, 9), (21, 16), (28, 23), (19, 21), (16, 29), (13, 21), (4, 23), (11, 16), (4, 9), (13, 11)],
            fill="#ffd27a", stroke="#c06a00")
    ic.circle(16, 16, 4, fill=WHITE, stroke="#c06a00")


@icon("makeunique")
def _(ic):
    box(ic, 4, 18, 9, 8, 4, front=WHITE, side=LIGHTGRAY, stroke=GRAY)
    box(ic, 15, 18, 9, 8, 4)
    ic.dot(26, 7, 3, fill=ACC)


@icon("lock")
def _(ic):
    ic.path("M10 15 L10 10 Q10 4 16 4 Q22 4 22 10 L22 15", width=2)
    ic.rect(7.5, 14.5, 17, 13, fill=GOLD, rx=1.5)
    ic.circle(16, 21, 1.8, fill=INK, stroke=None)


@icon("unlock")
def _(ic):
    ic.path("M10 15 L10 10 Q10 4 16 4 Q22 4 22 9", width=2)
    ic.rect(7.5, 14.5, 17, 13, fill="#f7e2a2", rx=1.5)
    ic.circle(16, 21, 1.8, fill=INK, stroke=None)


@icon("hide")
def _(ic):
    eye(ic, 16, 16, 13, closed=True)


@icon("unhide")
def _(ic):
    eye(ic, 16, 16, 13)


@icon("hiderest")
def _(ic):
    ic.rect(3.5, 5.5, 25, 22, fill="#f3f3f3", stroke=GRAY, dash="3 2")
    box(ic, 9, 16, 10, 8, 4)
    ic.line(4, 27, 9, 22, stroke=GRAY); ic.line(23, 10, 28, 6, stroke=GRAY)


@icon("reversefaces")
def _(ic):
    ic.poly([(4, 10), (16, 5), (16, 27), (4, 22)], fill=WHITE)
    ic.poly([(16, 5), (28, 10), (28, 22), (16, 27)], fill="#b8c4dd")
    ic.path("M8 4 Q16 -1 24 4", stroke=ACCD, width=1.4)
    ic.poly([(22, 1.5), (25.5, 5), (21, 6)], fill=ACCD, stroke=ACCD, width=0.6)


@icon("orientfaces")
def _(ic):
    box(ic, 5, 13, 15, 14, 7)
    for (x, y) in ((12, 20), (13, 9.5), (23.5, 17)):
        ic.dot(x, y, 1.6, fill=ACCD)


@icon("intersect")
def _(ic):
    ic.rect(4.5, 4.5, 15, 15, fill=TINT)
    ic.rect(12.5, 12.5, 15, 15, fill=WHITE)
    ic.rect(12.5, 12.5, 7, 7, fill=ACC, stroke=ACCD)


@icon("divide")
def _(ic):
    ic.line(3, 22, 29, 10, width=1.6)
    for t in (0.25, 0.5, 0.75):
        ic.dot(3 + 26 * t, 22 - 12 * t, 2.2, fill=RED)


@icon("weld")
def _(ic):
    ic.path("M3 24 Q9 8 16 16", width=1.6)
    ic.path("M16 16 Q23 24 29 8", stroke=ACCD, width=1.6)
    ic.dot(16, 16, 2.4, fill=GOLD)


@icon("explodecurve")
def _(ic):
    pts = [(3, 24), (8, 14), (15, 10), (22, 12), (28, 20)]
    for i in range(len(pts) - 1):
        (x1, y1), (x2, y2) = pts[i], pts[i + 1]
        ic.line(x1, y1, x2, y2, width=1.6)
    for x, y in pts:
        ic.dot(x, y, 1.8, fill=RED)


@icon("flip")
def _(ic):
    ic.line(16, 3, 16, 29, stroke=ACCD, dash="2.5 2")
    ic.poly([(4, 24), (13, 24), (13, 8)], fill=TINT)
    ic.poly([(28, 24), (19, 24), (19, 8)], fill=WHITE)


@icon("deleteguides")
def _(ic):
    ic.line(3, 26, 29, 6, stroke=INK, dash="3 2")
    ic.line(3, 14, 29, 14, stroke=INK, dash="3 2")
    ic.line(19, 19, 29, 29, stroke=RED, width=2.4); ic.line(29, 19, 19, 29, stroke=RED, width=2.4)


@icon("guides")
def _(ic):
    ic.line(3, 26, 29, 6, stroke=INK, dash="3 2")
    ic.line(3, 14, 29, 14, stroke=INK, dash="3 2")
    ic.dot(16, 14, 2.2, fill=INK)


# ---- solids
def _solid(ic, mode):
    A = dict(x=4.5, y=8.5, w=14, h=14)
    B = dict(x=13.5, y=13.5, w=14, h=14)
    if mode == "outer":
        ic.path("M4.5 8.5h14v5h9v14h-14v-5h-9z", fill=TINT, stroke=ACCD, width=1.4)
        ic.rect(13.5, 13.5, 5, 9, fill="none", stroke=GRAY, dash="1.5 1.5")
        return
    fa, fb, fx = {"union": (TINT, TINT, TINT), "subtract": (WHITE, TINT, WHITE),
                  "intersect": (WHITE, WHITE, ACC), "trim": (WHITE, TINT, WHITE),
                  "split": (TINT, TINT2, ACC)}[mode]
    ic.rect(A["x"], A["y"], A["w"], A["h"], fill=fa, stroke=GRAY if mode in ("subtract", "trim") else INK,
            dash="2 1.5" if mode in ("subtract",) else None)
    ic.rect(B["x"], B["y"], B["w"], B["h"], fill=fb)
    ic.rect(13.5, 13.5, 5, 9, fill=fx if mode != "subtract" else WHITE, stroke=ACCD if mode in ("intersect", "split") else INK)
    if mode == "union":
        ic.rect(13.5, 13.5, 5, 9, fill=TINT, stroke=None)
        ic.path("M4.5 8.5h14v5h9v14h-14v-5h-9z", fill="none", stroke=ACCD, width=1.4)
    if mode == "trim":
        ic.rect(A["x"], A["y"], A["w"], A["h"], fill="none", stroke=INK)


for _m in ("outer", "union", "subtract", "intersect", "trim", "split"):
    ICONS["solid_" + _m] = (lambda m: (lambda ic: _solid(ic, m)))(_m)


# ---- camera / view
for _v in ("iso", "top", "front", "right", "back", "left", "bottom"):
    ICONS["view_" + _v] = (lambda v: (lambda ic: view_cube(ic, v)))(_v)


@icon("perspective")
def _(ic):
    ic.poly([(5, 8), (27, 4), (27, 28), (5, 24)], fill=TINT)
    ic.poly([(11, 13), (21, 11.5), (21, 20.5), (11, 19)], fill=WHITE)
    for (x1, y1, x2, y2) in ((5, 8, 11, 13), (27, 4, 21, 11.5), (27, 28, 21, 20.5), (5, 24, 11, 19)):
        ic.line(x1, y1, x2, y2, width=0.9)


@icon("parallel")
def _(ic):
    ic.rect(4.5, 10.5, 16, 16, fill=TINT)
    ic.rect(11.5, 4.5, 16, 16, fill="none")
    for (x, y) in ((4.5, 10.5), (20.5, 10.5), (20.5, 26.5), (4.5, 26.5)):
        ic.line(x, y, x + 7, y - 6, width=0.9)


@icon("twopoint")
def _(ic):
    ic.line(2, 13, 30, 13, stroke=GRAY, dash="2 1.5")
    ic.poly([(16, 6), (27, 12), (27, 26), (16, 29)], fill=TINT2)
    ic.poly([(16, 6), (5, 12), (5, 26), (16, 29)], fill=TINT)


@icon("matchphoto")
def _(ic):
    ic.rect(3.5, 5.5, 25, 21, fill="#e4edf6")
    ic.path("M3.5 26.5 L3.5 19 L10 15 L16 18 L16 26.5 Z", fill=TINT, stroke=None)
    ic.line(16, 26.5, 16, 12, width=1.2)
    ic.line(16, 12, 28.5, 15.5, stroke=RED, width=1.6)
    ic.line(16, 19, 28.5, 20.5, stroke=RED, width=1.6)
    ic.line(16, 12, 3.5, 15.5, stroke=GREEN, width=1.6)
    ic.line(16, 19, 3.5, 20.5, stroke=GREEN, width=1.6)
    ic.rect(14, 24.5, 4, 4, fill=GOLD, stroke=INK, width=0.8)


@icon("fov")
def _(ic):
    camera(ic, 2, 11)
    ic.line(23, 13, 30, 5, stroke=ACCD, width=1.2); ic.line(23, 19, 30, 27, stroke=ACCD, width=1.2)
    ic.path("M28 7 Q31 16 28 25", stroke=ACCD, width=1)


@icon("alignview")
def _(ic):
    ic.poly([(4, 18), (16, 12), (28, 18), (16, 24)], fill=TINT)
    eye(ic, 16, 7, 7)
    arrow(ic, 16, 11, 16, 17, color=ACCD, width=1.2, head=2.5)


@icon("addscene")
def _(ic):
    ic.rect(3.5, 7.5, 21, 16, fill=WHITE)
    house(ic, 5, 8, 0.55)
    ic.circle(24, 23, 6, fill=GREEN, stroke=None)
    ic.line(24, 19.5, 24, 26.5, stroke=WHITE, width=1.8); ic.line(20.5, 23, 27.5, 23, stroke=WHITE, width=1.8)


@icon("prevscene")
def _(ic):
    ic.rect(9.5, 6.5, 19, 15, fill=WHITE)
    ic.poly([(8, 14), (2, 9.5), (2, 18.5)], fill=ACCD, stroke=ACCD)
    house(ic, 12, 7, 0.5)


@icon("nextscene")
def _(ic):
    ic.rect(3.5, 6.5, 19, 15, fill=WHITE)
    ic.poly([(24, 14), (30, 9.5), (30, 18.5)], fill=ACCD, stroke=ACCD)
    house(ic, 5.5, 7, 0.5)


@icon("playscenes")
def _(ic):
    ic.rect(3.5, 5.5, 25, 18, fill=WHITE)
    ic.poly([(12, 9), (22, 14.5), (12, 20)], fill=ACC, stroke=ACCD)
    ic.line(3.5, 27.5, 28.5, 27.5, stroke=GRAY, width=1.6)
    ic.line(3.5, 27.5, 13, 27.5, stroke=ACCD, width=1.6)


@icon("scenetabs")
def _(ic):
    ic.path("M3.5 12.5 L5.5 7.5 L13.5 7.5 L15.5 12.5", fill=WHITE)
    ic.path("M14.5 12.5 L16.5 7.5 L24.5 7.5 L26.5 12.5", fill=TINT)
    ic.rect(3.5, 12.5, 25, 14, fill=WHITE)


# face styles
for _f in ("wire", "hidden", "shaded", "textured", "mono", "xray", "backedges"):
    ICONS["fs_" + _f] = (lambda f: (lambda ic: faces_cube(ic, f)))(_f)


@icon("edges")
def _(ic):
    for p in ([(6, 11), (16, 6), (26, 11), (16, 16)], [(6, 11), (16, 16), (16, 27), (6, 22)], [(16, 16), (26, 11), (26, 22), (16, 27)]):
        ic.poly(p, fill=WHITE, width=1.4)


@icon("profiles")
def _(ic):
    for p in ([(6, 11), (16, 6), (26, 11), (16, 16)], [(6, 11), (16, 16), (16, 27), (6, 22)], [(16, 16), (26, 11), (26, 22), (16, 27)]):
        ic.poly(p, fill=WHITE, width=0.8)
    ic.poly([(6, 11), (16, 6), (26, 11), (26, 22), (16, 27), (6, 22)], fill="none", width=2.6)


@icon("extension")
def _(ic):
    ic.poly([(8, 12), (16, 8), (24, 12), (16, 16)], fill=WHITE)
    for (x1, y1, x2, y2) in ((8, 12, 4, 14), (16, 8, 12, 6), (16, 8, 20, 6), (24, 12, 28, 14), (16, 16, 16, 19)):
        ic.line(x1, y1, x2, y2, width=0.9)
    ic.poly([(8, 12), (16, 16), (16, 26), (8, 22)], fill=TINT)
    ic.poly([(16, 16), (24, 12), (24, 22), (16, 26)], fill=TINT2)


@icon("depthcue")
def _(ic):
    ic.line(4, 26, 28, 6, width=2.8)
    ic.line(4, 20, 18, 8, width=1.6)
    ic.line(10, 28, 28, 14, width=0.8)


@icon("showaxes")
def _(ic):
    axes_glyph(ic, 15, 19, 12)
    ic.line(15, 19, 25.8 - 21, 19 + 12 * 0.5 * -1 + 12, stroke=GREEN, width=0.8, dash="1.5 1.5")


@icon("hiddengeom")
def _(ic):
    ic.poly([(6, 11), (16, 6), (26, 11), (16, 16)], fill=WHITE, stroke=GRAY, dash="2 1.5")
    ic.poly([(6, 11), (16, 16), (16, 27), (6, 22)], fill=WHITE, stroke=GRAY, dash="2 1.5")
    ic.poly([(16, 16), (26, 11), (26, 22), (16, 27)], fill=WHITE, stroke=GRAY, dash="2 1.5")
    eye(ic, 16, 17, 8)


@icon("sectionplanes")
def _(ic):
    ic.poly([(4, 12), (20, 5), (28, 12), (12, 19)], fill="#ffd9a8", stroke="#c06a00")
    for (x, y) in ((4, 12), (20, 5), (28, 12), (12, 19)):
        ic.line(x, y, x, y - 3.5, stroke="#c06a00")
    ic.line(4, 25, 28, 25, stroke=GRAY, dash="2 1.5")


@icon("sectioncuts")
def _(ic):
    box(ic, 6, 16, 14, 11, 6)
    ic.poly([(6, 16), (12, 10), (26, 10), (20, 16)], fill="#ffb366", stroke="#c06a00", width=1.4)


@icon("sectionfill")
def _(ic):
    box(ic, 6, 16, 14, 11, 6)
    ic.poly([(6, 16), (12, 10), (26, 10), (20, 16)], fill=INK, stroke=INK)


@icon("shadows")
def _(ic):
    ic.poly([(13, 22), (29, 29), (21, 29), (6, 24)], fill="#8f8f8f", stroke=None)
    box(ic, 6, 15, 8, 10, 4)
    ic.circle(24, 8, 4.5, fill=GOLD, stroke="#b07a00")
    for a in range(0, 360, 45):
        r = math.radians(a)
        ic.line(24 + 6 * math.cos(r), 8 + 6 * math.sin(r), 24 + 7.8 * math.cos(r), 8 + 7.8 * math.sin(r), stroke="#b07a00", width=1)


@icon("fog")
def _(ic):
    house(ic, 3, 3, 0.85)
    for y, op in ((12, "0.75"), (18, "0.85"), (24, "0.95")):
        ic.parts.append(f'<rect x="0" y="{y}" width="32" height="3.5" rx="1.7" fill="#ffffff" opacity="{op}"/>')
        ic.line(2, y + 1.7, 30, y + 1.7, stroke=LIGHTGRAY, width=0.8)


@icon("skyground")
def _(ic):
    ic.rect(3.5, 4.5, 25, 12, fill="#cfe2f7", stroke=INK)
    ic.rect(3.5, 16.5, 25, 11, fill="#d8ccb4", stroke=INK)


# ---- panels / window
@icon("entityinfo")
def _(ic):
    ic.circle(16, 16, 12, fill=TINT, stroke=ACCD)
    ic.text(16, 22.5, "i", size=17, fill=ACCD, family="serif")


@icon("materials")
def _(ic):
    for (x, y, c) in ((9, 10, RED), (21, 9, GOLD), (10, 21, GREEN), (21, 21, BLUE)):
        ic.circle(x, y, 6, fill=c, stroke=INK, width=0.9)


@icon("components")
def _(ic):
    box(ic, 4, 13, 9, 8, 4)
    box(ic, 17, 13, 9, 8, 4)
    box(ic, 10, 26, 9, 5, 4, front=TINT2)


@icon("styles")
def _(ic):
    ic.rect(3.5, 5.5, 25, 21, fill=WHITE)
    ic.path("M3.5 26.5 L28.5 5.5 L28.5 26.5 Z", fill=TINT, stroke=None)
    ic.rect(3.5, 5.5, 25, 21, fill="none")
    house(ic, 7, 7, 0.6)


@icon("tags")
def _(ic):
    for k, f in enumerate((WHITE, TINT, TINT2)):
        y = 20 - k * 6
        ic.poly([(4, y), (16, y - 5), (28, y), (16, y + 5)], fill=f)


@icon("scenes")
def _(ic):
    ic.rect(3.5, 5.5, 25, 18, fill=WHITE)
    house(ic, 7, 5.5, 0.6)
    for x in (6, 13, 20):
        ic.rect(x - 0.5, 25.5, 6, 3, fill=TINT if x != 6 else ACC, stroke=INK, width=0.8)


@icon("outliner")
def _(ic):
    for k, (x, w) in enumerate(((4, 20), (9, 15), (9, 15), (14, 12))):
        y = 5 + k * 6.5
        ic.rect(x + 0.5, y + 0.5, 4, 4, fill=TINT if k else ACC, stroke=INK, width=0.8)
        ic.line(x + 6.5, y + 2.5, x + w + 4, y + 2.5, stroke=GRAY, width=1.4)


@icon("soften")
def _(ic):
    ic.path("M4 26 L4 14 Q4 6 12 6 L28 6", width=1.6)
    ic.path("M9 26 L9 16 Q9 11 14 11 L28 11", stroke=GRAY, width=0.9, dash="2 1.5")
    ic.path("M4 26 L28 26 L28 6", stroke=None, fill="none")


@icon("modelinfo")
def _(ic):
    ic.path("M7.5 3.5h12l6 6v19h-18z", fill=WHITE)
    ic.path("M19.5 3.5v6h6", fill="none")
    house(ic, 8.5, 11, 0.5)


@icon("preferences")
def _(ic):
    teeth = []
    for k in range(16):
        a = k * math.pi / 8
        r = 12 if k % 2 == 0 else 9.5
        teeth.append((16 + r * math.cos(a), 16 + r * math.sin(a)))
    ic.poly(teeth, fill=TINT)
    ic.circle(16, 16, 4, fill=WHITE)


@icon("shortcuts")
def _(ic):
    ic.rect(2.5, 8.5, 27, 16, fill=WHITE, rx=2)
    for r, n in ((11.5, 6), (16, 5)):
        for k in range(n):
            ic.rect(5 + k * 4.3 + (r - 11.5) * 0.4, r, 3, 3, fill=TINT, stroke=INK, width=0.6)
    ic.rect(9.5, 20.5, 13, 2.5, fill=TINT, stroke=INK, width=0.6)


@icon("about")
def _(ic):
    ic.circle(16, 16, 12, fill=WHITE, stroke=ACCD)
    ic.text(16, 22, "?", size=16, fill=ACCD)


@icon("help")
def _(ic):
    ICONS["about"](ic)


# ---- file / QAT (white versions are produced by recolouring at runtime)
@icon("new")
def _(ic):
    ic.path("M7.5 3.5h12l6 6v19h-18z", fill=WHITE)
    ic.path("M19.5 3.5v6h6", fill="none")


@icon("open")
def _(ic):
    ic.path("M3.5 8.5h9l2 2h12v15h-23z", fill=GOLD)
    ic.path("M3.5 25.5l4-10h22l-4 10z", fill="#f7e2a2")


@icon("save")
def _(ic):
    ic.path("M4.5 4.5h19l4 4v19h-23z", fill=TINT, stroke=ACCD)
    ic.rect(9.5, 4.5, 12, 8, fill=WHITE, stroke=ACCD)
    ic.rect(8.5, 17.5, 15, 10, fill=WHITE, stroke=ACCD)


@icon("saveas")
def _(ic):
    ICONS["save"](ic)
    pencil(ic, 18, 28)


@icon("import")
def _(ic):
    ic.path("M10.5 3.5h11l6 6v19h-17z", fill=WHITE)
    arrow(ic, 2, 16, 17, 16, color=GREEN, width=2, head=4.5)


@icon("export")
def _(ic):
    ic.path("M4.5 3.5h11l6 6v19h-17z", fill=WHITE)
    arrow(ic, 12, 16, 29, 16, color=ACCD, width=2, head=4.5)


@icon("exportimage")
def _(ic):
    ic.rect(3.5, 5.5, 25, 21, fill="#cfe2f7")
    ic.path("M3.5 26.5 L12 15 L18 21 L22 17 L28.5 24 L28.5 26.5 Z", fill=GREEN, stroke=INK, width=0.8)
    ic.circle(22, 11, 3, fill=GOLD, stroke=None)


@icon("print")
def _(ic):
    ic.rect(8.5, 3.5, 15, 9, fill=WHITE)
    ic.rect(3.5, 11.5, 25, 11, fill="#e3e3e3", rx=1.5)
    ic.rect(8.5, 18.5, 15, 10, fill=WHITE)
    ic.dot(24.5, 15, 1.2, fill=GREEN)


@icon("undo")
def _(ic):
    ic.path("M8 13 L22 13 Q28 13 28 19.5 Q28 26 22 26 L14 26", width=2.2)
    ic.poly([(3, 13), (10, 7), (10, 19)], fill=INK, stroke=INK, width=0.8)


@icon("redo")
def _(ic):
    ic.path("M24 13 L10 13 Q4 13 4 19.5 Q4 26 10 26 L18 26", width=2.2)
    ic.poly([(29, 13), (22, 7), (22, 19)], fill=INK, stroke=INK, width=0.8)


@icon("launcher")
def _(ic):
    ic.path("M8 8 L8 24 L24 24", width=1.6)
    arrow(ic, 12, 12, 23, 23, color=INK, width=1.6, head=5)


@icon("chevron")
def _(ic):
    ic.poly([(9, 12), (16, 19), (23, 12)], closed=False, width=2.2)


@icon("component_place")
def _(ic):
    box(ic, 7, 15, 12, 11, 6)
    arrow(ic, 26, 2, 21, 9, color=ACCD, head=3)


def main(out):
    os.makedirs(out, exist_ok=True)
    for name, fn in ICONS.items():
        for suffix, sw in (("", 1.0), ("-sm", 1.7)):
            ic = Icon(sw)
            fn(ic)
            with open(os.path.join(out, name + suffix + ".svg"), "w") as f:
                f.write(ic.svg())
    print(f"{len(ICONS)} icons -> {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "resources", "ribbon"))
