r"""The main menu's background photos: photos.py <out folder> <preview folder> [upscaled folder]

The upscaled folder, if given, has each source run through Real-ESRGAN (realesrgan-x4plus) as
<stem>.png; the boxes below are in the original's pixels and scale with it. Each source is
cropped to 16:9 (clear of the sites' watermarks), scaled to 2560x1440, and
darkened on the left where the menu is and along the bottom, as the menu's look has it. The
renderer reads them as ui\modern\photo1.tga.. (32-bit, bottom up) and crossfades between them.
"""
import os
import struct
import sys
from pathlib import Path

from PIL import Image, ImageFilter

W, H = 2560, 1440
# Where the sources were saved: MCM2_PHOTOS, else the Downloads folder.
DOWNLOADS = Path(os.environ.get("MCM2_PHOTOS") or (r"C:\Users\xshxd\Downloads" if os.name == "nt"
                                                   else Path.home() / "Downloads"))

# The sources and the 16:9 part of each to use (left, top, right, bottom).
SOURCES = [
    ("game-mast-motocross-madness-2-1024x443.jpg", (0, 0, 787, 443)),
    ("images (3).jpg", (0, 60, 516, 350)),
    ("motocross-madness-2.322442.jpg", (0, 20, 533, 320)),     # under the fragland mark
    ("original.jpeg", (0, 66, 598, 402)),                      # left of the Spong mark
]


def darken(image):
    """Dark on the left (the menu) and at the bottom, fading out to the right and up."""
    shade = Image.new("L", (W, H))
    across = [min(1.0, x / (W * 0.62)) for x in range(W)]
    down = [1.0 - 0.55 * max(0.0, (y - H * 0.7) / (H * 0.3)) for y in range(H)]
    shade.putdata([int(255 * (0.16 + 0.84 * (a ** 1.35)) * d) for d in down for a in across])
    black = Image.new("RGB", (W, H), (6, 7, 9))
    return Image.composite(image, black, shade)


def prepare(name, box, upscaled):
    image = Image.open(DOWNLOADS / name).convert("RGB")
    sharp = upscaled and upscaled / (Path(name).stem + ".png")
    if sharp and sharp.exists():
        scale = Image.open(sharp).width / image.width
        image = Image.open(sharp).convert("RGB")
        box = tuple(round(v * scale) for v in box)
    image = image.crop(box).resize((W, H), Image.LANCZOS)
    if not sharp:
        image = image.filter(ImageFilter.UnsharpMask(radius=4, percent=110, threshold=1))
    return darken(image)


def tga32(image):
    """An uncompressed 32-bit TGA, bottom row first."""
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, W, H, 32, 8)
    rgba = image.convert("RGBA").transpose(Image.FLIP_TOP_BOTTOM)
    r, g, b, a = rgba.split()
    return header + Image.merge("RGBA", (b, g, r, a)).tobytes()


if __name__ == "__main__":
    out, previews = Path(sys.argv[1]), Path(sys.argv[2])
    upscaled = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    for n, (name, box) in enumerate(SOURCES, 1):
        image = prepare(name, box, upscaled)
        (out / ("photo%d.tga" % n)).write_bytes(tga32(image))
        image.resize((W // 2, H // 2)).save(previews / ("menu photo %d.jpg" % n), quality=90)
        print("photo%d.tga from %s" % (n, name))
