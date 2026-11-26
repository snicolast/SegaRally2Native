# Draws res/pad/ps/*.png, the PlayStation-style buttons, from the Xbox ones in
# res/pad (their shapes, border and grey; CC0): the inside wiped, then a
# symbol or a name in white. Drawn for this port, no Sony artwork.
# Run from the repository's root: python res/pad/ps/src/make.py (Pillow;
# DejaVu Sans Bold, or pass another bold .ttf as the first argument)
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SRC = 'res/pad/'
OUT = 'res/pad/ps/'
GREY = (51, 51, 51, 255)
WHITE = (255, 255, 255)
S = 4   # drawn 4x, then scaled down
FONT = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


def blank(src):
    """the Xbox picture with its inside wiped: its 6 px border kept, grey inside"""
    im = Image.open(SRC + src).convert('RGBA')
    m = im.getchannel('A').point(lambda v: 255 if v > 128 else 0)
    pad = Image.new('L', (m.width + 20, m.height + 20), 0)   # (the edges of the picture count as outside)
    pad.paste(m, (10, 10))
    inner = pad.filter(ImageFilter.MinFilter(15)).crop((10, 10, 10 + m.width, 10 + m.height))
    out = im.copy()
    out.paste(GREY, (0, 0), inner)
    return out, inner


def white_ring(im, inner):
    ring = ImageChops.subtract(im.getchannel('A'), inner)
    out = im.copy()
    out.paste(WHITE + (255,), (0, 0), ring)
    return out


def symbol(kind, w=128, h=128):
    L = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(L)
    c = WHITE + (255,)
    cx, cy = w * S / 2, h * S / 2
    r, t = 34 * S, 12 * S
    if kind == 'cross':
        k = r * 0.92
        for sx in (-1, 1):
            d.line([(cx - k, cy - k * sx), (cx + k, cy + k * sx)], fill=c, width=t)
        for px, py in [(cx - k, cy - k), (cx + k, cy + k), (cx - k, cy + k), (cx + k, cy - k)]:   # round ends
            d.ellipse([px - t / 2, py - t / 2, px + t / 2, py + t / 2], fill=c)
    elif kind == 'circle':
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=c, width=t)
    elif kind == 'square':
        k = r * 0.86
        d.rounded_rectangle([cx - k, cy - k, cx + k, cy + k], radius=4 * S, outline=c, width=t)
    elif kind == 'triangle':
        R, cy2 = r * 1.3, cy + r * 1.3 * 0.2
        corner = lambda rad: [(cx + rad * math.cos(math.radians(a)), cy2 + rad * math.sin(math.radians(a))) for a in (-90, 30, 150)]
        d.polygon(corner(R), fill=c)
        d.polygon(corner(R - t * 2.0), fill=(0, 0, 0, 0))
    elif kind == 'options':   # three lines
        for i in (-1, 0, 1):
            y = cy + i * 15 * S
            d.rounded_rectangle([cx - 26 * S, y - 4 * S, cx + 26 * S, y + 4 * S], radius=4 * S, fill=c)
    elif kind == 'share':     # an arrow up out of a tray
        d.line([(cx, cy - 30 * S), (cx, cy + 8 * S)], fill=c, width=8 * S)
        d.polygon([(cx - 18 * S, cy - 12 * S), (cx, cy - 34 * S), (cx + 18 * S, cy - 12 * S)], fill=c)
        d.line([(cx - 28 * S, cy + 2 * S), (cx - 28 * S, cy + 26 * S), (cx + 28 * S, cy + 26 * S), (cx + 28 * S, cy + 2 * S)],
               fill=c, width=8 * S, joint='curve')
    return L.resize((w, h), Image.LANCZOS)


def label(text, w, h, size):
    L = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(L)
    f = ImageFont.truetype(FONT, size * S)
    bb = d.textbbox((0, 0), text, font=f)
    d.text(((w * S - (bb[2] - bb[0])) / 2 - bb[0], (h * S - (bb[3] - bb[1])) / 2 - bb[1]), text, font=f, fill=WHITE + (255,))
    return L.resize((w, h), Image.LANCZOS)


def save(im, name):
    im.save(OUT + name + '.png', optimize=True)


os.makedirs(OUT, exist_ok=True)
for kind in ('cross', 'circle', 'square', 'triangle'):   # the face buttons: on a's disc
    im, inner = blank('a.png')
    im = white_ring(im, inner)
    im.alpha_composite(symbol(kind))
    save(im, kind)
for name, src in (('l1', 'lb.png'), ('r1', 'rb.png'), ('l2', 'lt.png'), ('r2', 'rt.png')):   # the shoulders' shapes
    im, _ = blank(src)
    im.alpha_composite(label(name.upper(), im.width, im.height, 70))
    save(im, name)
for name in ('l3', 'r3', 'options', 'share'):   # the sticks and the middle buttons: on menu's disc
    im, inner = blank('menu.png')
    im = white_ring(im, inner)
    im.alpha_composite(label(name.upper(), 128, 128, 54) if name in ('l3', 'r3') else symbol(name))
    save(im, name)
