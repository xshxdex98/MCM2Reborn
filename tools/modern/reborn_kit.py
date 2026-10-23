r"""MCM2:Reborn's look, shared by the screens' builders and mock-ups: the grid, colours, type and
shapes.

Headings are in the heading face (AeromoveDemo: letters only, it has no figures), everything
else in the text face (Helvetica Oblique); fonts_from() finds them in the game's ui\modernonts,
which the dev build loads for the game too. A screen is 2560x1440 on one grid: 96 px side
margins, 64 px top and bottom, one 40 px gutter between panels.
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from paths import FONTS, find_folder  # noqa: E402  (the Windows fonts: MCM2_FONTS, tools/paths.py)

SYMBOLS = find_folder(FONTS, "seguisym.ttf")

W, H = 2560, 1440
LEFT, RIGHT = 96, 2464
TOP, BOTTOM = 64, 1376
# The game's name and version, wherever the screens (and the launcher) show them.
TITLE = "MCM2:REBORN"
VERSION = re.search(r'#define GAME_VERSION "([^"]+)"', (Path(__file__).parents[2] / "code" / "dev" / "dev.h").read_text()).group(1)

GUTTER = 40
SLANT = 0.21                        # how far cut edges lean: about 12 degrees

ORANGE = (255, 138, 0)
GOLD = (255, 196, 0)
INK = (14, 14, 16)
PAPER = (244, 240, 230)
SOFT = (200, 200, 204)
BODY = (196, 196, 202)
GREY = (150, 150, 156)
DIM = (96, 98, 104)
PANEL = (16, 17, 20)
RAISED = (30, 31, 36)
EDGE = (64, 66, 72)
RED = (255, 64, 44)
SELECTED = (42, 33, 22)             # a chosen row's ground

HEADING = TEXT = None


HEADING_FILE = "AeromoveDemoRegular.ttf"
TEXT_FILE = "Helvetica-Oblique.ttf"


def set_fonts(heading_font, text_font):
    global HEADING, TEXT, _figure_scale
    HEADING, TEXT = str(heading_font), str(text_font)
    _figure_scale = None


def fonts_from(folder):
    """The two faces from `folder`: the game's modern fonts folder."""
    from pathlib import Path
    set_fonts(Path(folder) / HEADING_FILE, Path(folder) / TEXT_FILE)


# ---- Type ----

def stroke(size):
    return max(1, size // 22)


def sub_font(size):
    return ImageFont.truetype(TEXT, size)


def sub(img, words, x, y, size, colour, anchor="ls", bold=False):
    """Text-face type from its baseline at (x, y); `bold` thickens it (the face has one weight)."""
    ImageDraw.Draw(img).text((x, y), words, font=sub_font(size), fill=colour, anchor=anchor,
                             stroke_width=stroke(size) if bold else 0, stroke_fill=colour)


def sub_width(words, size, bold=False):
    return sub_font(size).getlength(words) + (2 * stroke(size) if bold else 0)


def wrap(words, size, width):
    """`words` broken into lines of the text face no wider than `width`."""
    lines, line = [], ""
    f = sub_font(size)
    for word in words.split():
        if line and f.getbbox(line + " " + word, anchor="ls")[2] > width:
            lines.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    return lines + [line] if line else lines


APOSTROPHES = ("'", "\u2019")


def heading_runs(words, size):
    """`words` in runs, each with the face it is set in and how far it advances: letters in the
    heading face, figures and marks in the text face (an apostrophe set close), spaces a third
    of the size."""
    face = ImageFont.truetype(HEADING, size)
    runs = []
    for ch in words:
        kind = "space" if ch == " " else "letter" if ch.isalpha() else "mark"
        if runs and runs[-1][0] == kind:
            runs[-1][1] += ch
        else:
            runs.append([kind, ch])
    out = []
    for kind, run in runs:
        if kind == "space":
            out.append((run, None, size * 0.34 * len(run)))
        elif kind == "letter":
            out.append((run, face, face.getlength(run)))
        else:
            # Figures as tall as the heading's capitals, so "250SX" reads as one word.
            f = sub_font(max(8, round(size * figure_scale())))
            close = 0.6 if run in APOSTROPHES else 1
            out.append((run, f, f.getlength(run) * close + 2 * stroke(size)))
    return out


_figure_scale = None


def figure_scale():
    """The text face's size, for a size of the heading face, at which their capitals and
    figures stand equally tall."""
    global _figure_scale
    if _figure_scale is None:
        heading_cap = -ImageFont.truetype(HEADING, 200).getbbox("H", anchor="ls")[1]
        figure = -sub_font(200).getbbox("0", anchor="ls")[1]
        _figure_scale = heading_cap / figure
    return _figure_scale


def heading_width(words, size):
    return sum(advance for _, _, advance in heading_runs(words, size))


def heading_ink(words, size):
    """How far a heading's ink reaches from its start: its width, plus whatever its last glyph
    leans out past its advance (italic letters do), so nothing is measured short."""
    runs = heading_runs(words, size)
    width = sum(advance for _, _, advance in runs)
    last = next((r for r in reversed(runs) if r[1] is not None), None)
    if last:
        run, f, advance = last
        width += max(0, f.getbbox(run, anchor="ls")[2] - f.getlength(run))
    return width


def fit(words, size, width, least=12):
    """The largest size up to `size` at which a heading's ink fits `width`."""
    while size > least and heading_ink(words, size) > width:
        size -= 1
    return size


def sub_fit(words, size, width, least=10, bold=False):
    """The same for text-face type."""
    while size > least:
        f = sub_font(size)
        if f.getbbox(words, anchor="ls")[2] + (2 * stroke(size) if bold else 0) <= width:
            break
        size -= 1
    return size


def heading(img, words, x, y, size, colour, anchor="ls"):
    """A heading from its baseline at (x, y); "ms" or "rs" centre or right it on x."""
    if anchor == "ms":
        x -= heading_width(words, size) / 2
    elif anchor == "rs":
        x -= heading_width(words, size)
    d = ImageDraw.Draw(img)
    for run, f, advance in heading_runs(words, size):
        if f is not None:
            mark = f.path != HEADING
            shift = -size * 0.08 if run in APOSTROPHES else 0
            d.text((x + shift, y), run, font=f, fill=colour, anchor="ls",
                   stroke_width=stroke(size) if mark else 0, stroke_fill=colour)
        x += advance


def cap_height(size):
    return -ImageFont.truetype(HEADING, size).getbbox("H", anchor="ls")[1]


# ---- Shapes ----

def slanted(area, cut=SLANT):
    """A parallelogram over `area`, its left and right edges leant."""
    x0, y0, x1, y1 = area
    d = (y1 - y0) * cut
    return [(x0 + d, y0), (x1, y0), (x1 - d, y1), (x0, y1)]


def shape(img, points, fill, alpha=255):
    layer = Image.new("RGBA", img.size)
    ImageDraw.Draw(layer).polygon(points, fill=fill + (alpha,))
    img.alpha_composite(layer)


def outline(img, points, colour, width):
    layer = Image.new("RGBA", img.size)
    ImageDraw.Draw(layer).line(points + [points[0]], fill=colour + (255,), width=width, joint="curve")
    img.alpha_composite(layer)


def box(img, area, fill, radius=0, line=None):
    layer = Image.new("RGBA", img.size)
    ImageDraw.Draw(layer).rounded_rectangle(area, radius, fill=fill, outline=line, width=2 if line else 0)
    img.alpha_composite(layer)


def panel(img, area, alpha=210):
    box(img, area, PANEL + (alpha,), 10)


def dot(img, centre, radius, colour):
    layer = Image.new("RGBA", img.size)
    x, y = centre
    ImageDraw.Draw(layer).ellipse((x - radius, y - radius, x + radius, y + radius), fill=colour + (255,))
    img.alpha_composite(layer)


def cover(picture, size, keep_top=False):
    """`picture` filling `size`, cropped about its middle (or from its top, `keep_top`)."""
    w, h = size
    scale = max(w / picture.width, h / picture.height)
    p = picture.resize((int(picture.width * scale + 1), int(picture.height * scale + 1)), Image.LANCZOS)
    x = (p.width - w) // 2
    y = 0 if keep_top else (p.height - h) // 2
    return p.crop((x, y, x + w, y + h))


def fade_down(face, start=0.45, strength=235):
    """`face` darkening towards its bottom, for type over it."""
    shade = Image.new("L", face.size)
    d = ImageDraw.Draw(shade)
    for y in range(face.height):
        d.line((0, y, face.width, y), fill=int(strength * max(0, (y - face.height * start) /
                                                              (face.height * (1 - start)))))
    return Image.composite(Image.new("RGBA", face.size, (10, 10, 12, 255)), face.convert("RGBA"), shade)


def paste_shape(img, face, area, cut=SLANT):
    """`face` (sized to `area`) pasted inside the parallelogram over `area`."""
    mask = Image.new("L", img.size)
    ImageDraw.Draw(mask).polygon(slanted(area, cut), fill=255)
    layer = Image.new("RGBA", img.size)
    layer.paste(face, (int(area[0]), int(area[1])))
    img.paste(layer, (0, 0), mask)


def tag(img, words, x, y, size=24, fill=ORANGE, colour=INK):
    """A small slanted label at (x, y), as wide as its words; its right edge back."""
    w = int(heading_width(words, size)) + 52
    h = int(size * 1.6)
    shape(img, slanted((x, y, x + w, y + h)), fill)
    heading(img, words, x + w / 2 + 2, y + h / 2 + cap_height(size) / 2, size, colour, anchor="ms")
    return x + w


def plate(img, x, y, number, on, w=88, h=64, size=36):
    """A dirt-bike number plate: rounded, its number black on yellow, or grey at rest."""
    layer = Image.new("RGBA", img.size)
    d = ImageDraw.Draw(layer)
    d.rounded_rectangle((x, y, x + w, y + h), h // 4, fill=(GOLD if on else (58, 60, 66)) + (255,))
    d.rounded_rectangle((x + 6, y + 6, x + w - 6, y + h - 6), h // 6,
                        outline=(INK if on else (92, 94, 100)) + (110,), width=2)
    img.alpha_composite(layer)
    sub(img, number, x + w / 2, y + h / 2, size, INK if on else (180, 181, 186), anchor="mm", bold=True)


def keycap(img, x, centre, key, label):
    """A key and what it does, the key's middle on `centre`; the x after them."""
    f = ImageFont.truetype(SYMBOLS, 26) if any(ord(c) > 0x2000 for c in key) else sub_font(24)
    w = max(52, int(f.getlength(key)) + 28)
    box(img, (x, centre - 22, x + w, centre + 22), (236, 232, 222, 235), 8)
    ImageDraw.Draw(img).text((x + w / 2, centre), key, font=f, fill=INK, anchor="mm")
    sub(img, label, x + w + 16, centre, 26, GREY, anchor="lm")
    return x + w + 16 + int(sub_width(label, 26)) + 52


def keys(img, pairs, centre, x=LEFT):
    for key, label in pairs:
        x = keycap(img, x, centre, key, label)
    return x


def action(img, words, area, primary=True):
    """A slanted button over `area`: orange, or an outlined ghost; its words and a chevron centred."""
    x0, y0, x1, y1 = area
    size = int((y1 - y0) * 0.6)
    if primary:
        shape(img, slanted(area), ORANGE)
    else:
        shape(img, slanted(area), PANEL, 200)
        outline(img, slanted(area), EDGE, 2)
    colour = INK if primary else PAPER
    total = heading_width(words, size) + 14 + sub_width("\u203a", int(size * 1.15), bold=True)
    lx = (x0 + x1) / 2 - total / 2
    baseline = (y0 + y1) / 2 + cap_height(size) / 2
    heading(img, words, lx, baseline, size, colour)
    sub(img, "\u203a", lx + heading_width(words, size) + 14, baseline + 2, int(size * 1.15), colour, bold=True)


def chip(img, area, label, value):
    """A small setting: its name above its value."""
    x0, y0, x1, y1 = area
    box(img, area, RAISED + (255,), 8, EDGE + (255,))
    sub(img, label, x0 + 16, y0 + 22, 18, GREY)
    sub(img, value, x0 + 16, y0 + 48, 24, PAPER, bold=True)


def photo_background(path, darkness=175, blur=6, glow=True):
    """A menu photo, softened and darkened, with the warm glow from the bottom left."""
    img = cover(Image.open(path).convert("RGB"), (W, H)).convert("RGBA")
    if blur:
        img = img.filter(ImageFilter.GaussianBlur(blur))
    img = Image.alpha_composite(img, Image.new("RGBA", (W, H), (8, 8, 10, darkness)))
    if glow:
        light = Image.new("L", (W, H))
        ImageDraw.Draw(light).ellipse((-900, 700, 1300, 2300), fill=90)
        light = light.filter(ImageFilter.GaussianBlur(220))
        img = Image.composite(Image.new("RGBA", (W, H), ORANGE + (255,)), img, light.point(lambda v: v // 3))
    return img


def page_title(img, kicker, title, size=92):
    """A screen's kicker tag, its title and the orange rule under it."""
    tag(img, kicker, LEFT, TOP, 26)
    heading(img, title, LEFT, TOP + 140, size, PAPER)
    shape(img, [(LEFT + 2, TOP + 162), (LEFT + 600, TOP + 162), (LEFT + 594, TOP + 170), (LEFT, TOP + 170)],
          ORANGE)


def logo(img, area):
    """The supplied Reborn artwork, with its aspect ratio and transparency preserved."""
    from PIL import ImageOps
    art = Image.open(Path(__file__).parents[2] / "assets" / "logo.png").convert("RGBA")
    x0, y0, x1, y1 = area
    art = ImageOps.contain(art, (int(x1 - x0), int(y1 - y0)), Image.Resampling.LANCZOS)
    img.alpha_composite(art, (int(x0), int(y0 + (y1 - y0 - art.height) / 2)))


def build_mark(img):
    logo(img, (RIGHT - 360, TOP - 34, RIGHT - 100, TOP + 30))
    sub(img, "v%s" % VERSION, RIGHT, TOP + 18, 22, DIM, anchor="rs")


STEPS = [("01", "EVENT"), ("02", "BIKE & RIDER"), ("03", "RACE INFO")]


def step_areas(top=TOP + 54, names=STEPS):
    """Where a screen's steps go: one size, ending at the right margin."""
    tab_h, gap, inner = 72, 20, 34                  # `inner`: the room a tab keeps either side
    number_w = max(sub_width(n, 34, bold=True) for n, _ in names)
    tab_w = int(max(number_w + 18 + heading_ink(name, 32) for _, name in names) + 2 * inner
                + tab_h * SLANT)
    x = RIGHT - len(names) * tab_w - (len(names) - 1) * gap
    return [(x + i * (tab_w + gap), top, x + i * (tab_w + gap) + tab_w, top + tab_h) for i in range(len(names))]


def steps(img, active, top=TOP + 54, names=STEPS):
    """A screen's steps (the single player's by default), each number and name centred in its tab."""
    number_w = max(sub_width(n, 34, bold=True) for n, _ in names)
    for i, ((number, name), area) in enumerate(zip(names, step_areas(top, names))):
        x, y0, x1, y1 = area
        on = i == active
        pts = slanted(area)
        if on:
            shape(img, pts, ORANGE)
        else:
            shape(img, pts, PANEL, 200)
            outline(img, pts, (72, 74, 80), 2)
        content = number_w + 18 + heading_ink(name, 32)
        cx = (x + x1) / 2 - content / 2
        baseline = (y0 + y1) / 2 + cap_height(32) / 2
        sub(img, number, cx, baseline, 34, INK if on else ORANGE, bold=True)
        heading(img, name, cx + number_w + 18, baseline, 32, INK if on else PAPER)


def keys_right(img, pairs, centre, right):
    """Key hints ending at `right`."""
    probe = Image.new("RGBA", (W, H))
    width = keys(probe, pairs, 0, 0) - 52
    return keys(img, pairs, centre, right - width)
