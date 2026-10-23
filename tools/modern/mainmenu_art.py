"""The modern menus' look, drawn from nothing.

Flat and graphic: a dark backdrop, condensed italic type in cream, a yellow for what is chosen,
a blue for labels, and grey rows with a blue edge. Everything is laid out on a 1920x1080 grid
and drawn at SCALE times that, so the pictures are sharp at the size the menus show at
(2560x1440: SCALE 4/3).
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from paths import FONTS, find_folder  # noqa: E402  (the Windows fonts: MCM2_FONTS, tools/paths.py)

SCALE = 4 / 3
W, H = round(1920 * SCALE), round(1080 * SCALE)

BAHNSCHRIFT = find_folder(FONTS, "bahnschrift.ttf")
CONSOLAS = find_folder(FONTS, "consola.ttf")
SEGOE = find_folder(FONTS, "segoeui.ttf")
SEGOE_SEMIBOLD = find_folder(FONTS, "seguisb.ttf")

CREAM = (240, 236, 224)
YELLOW = (245, 211, 39)
YELLOW_DOWN = (214, 182, 22)
BLUE = (74, 144, 226)
INK = (18, 18, 20)              # type on yellow
GREY = (150, 152, 156)          # quiet type
DIM = (96, 99, 104)             # a greyed-out item
ROW = (62, 65, 70)              # a row at rest
ROW_LINE = (78, 82, 88)         # the rule between rows
CARD = (52, 55, 59)


def s(value):
    """A length on the 1920x1080 grid, in pixels."""
    return round(value * SCALE)


def font(size, path=BAHNSCHRIFT, style="Bold Condensed"):
    f = ImageFont.truetype(path, s(size))
    if path == BAHNSCHRIFT:
        f.set_variation_by_name(style)
    return f


def mono(size):
    return font(size, CONSOLAS)


def body(size, semibold=False):
    return font(size, SEGOE_SEMIBOLD if semibold else SEGOE)


def backdrop():
    """Near black at the top to a dark slate at the bottom."""
    sky = np.linspace(0, 1, H)[:, None]
    top, bottom = np.array((14, 15, 18)), np.array((34, 37, 42))
    rows = top + (bottom - top) * sky
    return Image.fromarray(np.repeat(rows[:, None, :], W, 1).astype(np.uint8)).convert("RGBA")


def lean(img, slant=0.18):
    """Lean an image forward, as italic type does."""
    w, h = img.size
    extra = int(h * slant)
    return img.transform((w + extra, h), Image.AFFINE, (1, slant, -extra, 0, 1, 0), Image.BICUBIC)


def width_of(words, f, spacing=0):
    """How wide `words` set, in pixels; `spacing` (grid units) goes between letters."""
    return sum(f.getlength(c) for c in words) + s(spacing) * max(len(words) - 1, 0)


def text(canvas, words, x, y, f, colour, italic=False, right=False, spacing=0):
    """`words` with its baseline at y pixels, starting at x (or ending there, `right`)."""
    ascent, descent = f.getmetrics()
    pad = s(12)
    advance = width_of(words, f, spacing)
    mask = Image.new("L", (int(advance) + pad * 2, ascent + descent + pad * 2))
    d = ImageDraw.Draw(mask)
    at = pad
    for c in words:
        d.text((at, pad + ascent), c, font=f, fill=255, anchor="ls")
        at += f.getlength(c) + s(spacing)
    if italic:
        mask = lean(mask)
    left = x - (int(advance) if right else 0) - pad
    layer = Image.new("RGBA", mask.size, colour + (0,))
    layer.putalpha(mask)
    canvas.alpha_composite(layer, (int(left), int(y - ascent - pad)))


def rect(canvas, box, colour, alpha=255):
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).rectangle(box, fill=colour + (alpha,))
    canvas.alpha_composite(layer)


def dash(canvas, x, y, length, colour):
    """The short rule before a label."""
    rect(canvas, (x, y, x + s(length) - 1, y + s(3) - 1), colour)


def arrow(canvas, x, y, size, colour):
    """A right arrow, centred on (x, y)."""
    d = ImageDraw.Draw(canvas)
    half = s(size) // 2
    w = max(s(2.4), 2)
    d.line([(x - half, y), (x + half, y)], fill=colour + (255,), width=w)
    d.line([(x + half - s(size * 0.42), y - s(size * 0.42)), (x + half, y),
            (x + half - s(size * 0.42), y + s(size * 0.42))], fill=colour + (255,), width=w,
           joint="curve")


def exit_icon(canvas, x, y, colour):
    """A door with an arrow leaving it, top left at (x, y)."""
    d = ImageDraw.Draw(canvas)
    w = max(s(2), 2)
    d.line([(x + s(9), y), (x, y), (x, y + s(18)), (x + s(9), y + s(18))], fill=colour + (255,),
           width=w)
    d.line([(x + s(6), y + s(9)), (x + s(19), y + s(9))], fill=colour + (255,), width=w)
    d.line([(x + s(15), y + s(5)), (x + s(19), y + s(9)), (x + s(15), y + s(13))],
           fill=colour + (255,), width=w)
