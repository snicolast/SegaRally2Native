"""Builds the high-resolution text glyphs (src/video/text_hd.h): the game's
pixel-art letters and digits are redrawn with free (SIL OFL) vector fonts.

Usage:
  python tools/make_text_atlas.py [snapshot.bin] [--preview out/text_preview.png]

The snapshot (F9 in the game, out/snap/*.bin) provides the tile patterns of
the fonts; Sega Rally 2's race HUD (8 bpp tiles, layer A) needs one taken in
a race. Each glyph listed below is drawn with its font at 8x, fitted to the
box of the original pixel art, and cut into the game's 8x8 tiles. Writes:
  textures/text/atlas.png   per tile: R = letter, G = outline/shadow,
                            B = top-to-bottom position for the gradient
  textures/text/glyphs.txt  pattern, hash of its pixels, atlas slot and the
                            palette entries of the three colours ("B" lines:
                            8 bpp patterns, palette entries 1-255; plain lines:
                            4 bpp, nibbles)
The colours are not stored: the game picks them from its palette at run time,
so fades and highlighted lines still work. Needs Pillow and numpy.
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import lwhash  # tools/ is on sys.path when run as `python tools/<script>.py`

FONTS = "third_party/fonts"
OUT = "textures/text"
SCALE = 8          # atlas pixels per original pixel
SS = 4             # supersampling of the font rasteriser
PAD = 1            # original pixels of margin around each tile in the atlas
SLOT = (8 + 2 * PAD) * SCALE
COLS = 16          # slots per atlas row (shader: ATLAS_COLS)
VRAM_SIZE = 0x190000
LINE_SCROLL = 0xF6000   # patterns end where the line scroll tables start

args = sys.argv[1:]
preview = None
if "--preview" in args:
    k = args.index("--preview")
    preview = args[k + 1]
    del args[k:k + 2]
snap = args[0] if args else "out/snap/snap_2271.bin"
vram = open(snap, "rb").read()[-VRAM_SIZE:]


# ---- Tiles -------------------------------------------------------------------
# Glyph cells are name table entries, as the game writes them. 8 bpp (Sega
# Rally 2's HUD): pattern = entry & 0x3FFF (64 bytes each, the pixels of each
# 32-bit word from its high byte), palette = (entry & 0x7F00) | pixel.
# 4 bpp: pattern = (entry & 0x3FFF) << 1 | entry >> 15 (32 bytes), palette =
# (entry & 0x7FF0) | nibble.

def pattern_number(e, bpp):
    return e & 0x3FFF if bpp == 8 else ((e & 0x3FFF) << 1) | (e >> 15)


def pattern_bytes(e, bpp):
    size = 64 if bpp == 8 else 32
    start = (pattern_number(e, bpp) * size) % LINE_SCROLL
    return vram[start:start + size]


def pattern(e, bpp):
    """8x8 palette indices (within the tile's bank) of entry e"""
    data = pattern_bytes(e, bpp)
    rows = []
    for y in range(8):
        if bpp == 8:
            rows.append([data[y * 8 + (x & 4) + 3 - (x & 3)] for x in range(8)])
        else:
            w = struct.unpack_from("<I", data, y * 4)[0]
            rows.append([(w >> ((7 - x) * 4)) & 15 for x in range(8)])
    return np.array(rows)


def pattern_hash(e, bpp):
    return lwhash.fnv1a_bytes(pattern_bytes(e, bpp))


def entry_base(e, bpp):
    return e & (0x7F00 if bpp == 8 else 0x7FF0)


def palette_rgb(index):
    c = struct.unpack_from("<I", vram, 0x100000 + index * 4)[0]
    return (c & 31), (c >> 5) & 31, (c >> 10) & 31


def palette_luma(base, n):
    r, g, b = palette_rgb(base | n)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


# ---- Glyphs ----------------------------------------------------------------
# A glyph is a character (or a whole word) and its grid of name table entries
# (rows of columns). Sets share a font, a size and an edge style:
#   flat     one colour
#   shadow   drop shadow below and to the right
#   outline  dark outline all around
# "fit": "box": each glyph (a word) is stretched to the box of the original
# art instead of sharing one size

def grid(*rows):
    return [list(r) for r in rows]


def row(first, count):
    return list(range(first, first + count))


def digit_2x3(first):
    return grid(row(first, 2), row(first + 2, 2), row(first + 4, 2))


# Sega Rally 2's race HUD (layer A, 8 bpp)
# The times (TOTAL TIME, LAP TIME, practice's laps): white italic digits
# 2x3 tiles from 0x2952, six tiles a digit; the minute and second marks
time_glyphs = [(str(k), digit_2x3(0x2952 + 6 * k)) for k in range(10)]
time_glyphs += [("’", grid([0x2991], [0x2992])), ("”", grid([0x2993], [0x2994]))]
# A time not set yet (the laps to come): a dash over two tiles
dash_glyphs = [("-", grid([0x298F, 0x2990]))]
# The red dots after DRIVER (no name entered)
dot_glyphs = [(".", grid([0x275D]))]
# The speed: the same digits from 0x3014, and "km/h"
speed_glyphs = [(str(k), digit_2x3(0x3014 + 6 * k)) for k in range(10)]
units_glyphs = [("km/h", grid([0x300B, 0x3000, 0x3000, 0x3000], row(0x300C, 4), row(0x3010, 4)))]
# The small font (stage top 3, and others): one tile a character
small_glyphs = [("”", grid([0x2821])), ("’", grid([0x2822])), ("-", grid([0x2823])), (".", grid([0x2824]))]
small_glyphs += [(str(k), grid([0x2825 + k])) for k in range(10)]
small_glyphs += [(chr(ord("A") + k), grid([0x282F + k])) for k in range(26)]
# The red labels: pictures of whole words
label_glyphs = [
    ("TOTAL TIME", grid(row(0x2900, 11), row(0x290B, 11))),
    ("LAP TIME", grid(row(0x2916, 9), row(0x291F, 9))),
    ("POSITION", grid(row(0x2D00, 9), row(0x2D09, 9))),
    ("STAGE TOP 3", grid(row(0x2800, 11), row(0x280B, 11))),
]

SETS = [
    # (the digits narrowed to the original's box: their italic and outline
    # would be cut at the edges of its tiles)
    {"name": "time", "font": "Exo2-Italic[wght].ttf", "weight": 800, "edge": "outline", "bpp": 8,
     "glyphs": time_glyphs, "same_roles": True, "max_width": 0.84, "outline": 1.2, "inset": 1.5},
    {"name": "dash", "font": "Exo2-Italic[wght].ttf", "weight": 800, "edge": "outline", "bpp": 8,
     "glyphs": dash_glyphs, "fit": "box", "outline": 1.0},
    {"name": "dot", "font": "ArchivoBlack-Regular.ttf", "edge": "outline", "bpp": 8,
     "glyphs": dot_glyphs, "fit": "box", "outline": 1.0},
    {"name": "speed", "font": "Exo2-Italic[wght].ttf", "weight": 800, "edge": "outline", "bpp": 8,
     "glyphs": speed_glyphs, "same_roles": True, "max_width": 0.84, "outline": 1.2, "inset": 1.5},
    {"name": "units", "font": "Exo2-Italic[wght].ttf", "weight": 800, "edge": "outline", "bpp": 8,
     "glyphs": units_glyphs, "fit": "box", "outline": 1.0, "inset": 1.0},
    {"name": "small", "font": "Exo2[wght].ttf", "weight": 800, "edge": "shadow", "bpp": 8,
     "glyphs": small_glyphs, "same_roles": True},
    {"name": "labels", "font": "ArchivoBlack-Regular.ttf", "edge": "outline", "bpp": 8,
     "glyphs": label_glyphs, "fit": "box", "same_roles": True},
]
for s in SETS:
    s["font"] = os.environ.get("LW_FONT_" + s["name"].upper(), s["font"])


# ---- Analysis of the original pixel art ------------------------------------

def original(glyph, bpp):
    """Palette indices of the whole glyph and the palette base of its first tile"""
    tiles = glyph[1]
    img = np.vstack([np.hstack([pattern(e, bpp) for e in r]) for r in tiles])
    return img, entry_base(tiles[0][0], bpp)


def roles(img, base, edge_style):
    """Palette entries of the gradient's top and bottom and of the edge"""
    used = [n for n in np.unique(img) if n]
    counts = {n: int((img == n).sum()) for n in used}
    edge = 0
    if edge_style != "flat" and len(used) > 1:
        edge = min(used, key=lambda n: palette_luma(base, n))
    body = [n for n in used if n != edge]
    total = sum(counts[n] for n in body)
    # the body's colours by height: the top of the gradient is what covers
    # the upper rows, the bottom what covers the lower ones
    ys = np.arange(img.shape[0])[:, None].repeat(img.shape[1], 1)
    mean_y = {n: float(ys[img == n].mean()) for n in body}
    big = [n for n in body if counts[n] * 8 >= total] or body
    top = min(big, key=lambda n: mean_y[n])
    bottom = max(big, key=lambda n: mean_y[n])
    return int(top), int(bottom), int(edge)


def body_box(img, edge):
    ys, xs = np.nonzero((img != 0) & (img != edge))
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


# ---- Drawing ---------------------------------------------------------------

def dilate(mask, r):
    """Round dilation of a coverage mask by r pixels"""
    if r <= 0:
        return mask
    out = mask.copy()
    ir = int(np.ceil(r))
    for dy in range(-ir, ir + 1):
        for dx in range(-ir, ir + 1):
            d = np.hypot(dx, dy)
            if d > r + 0.5:
                continue
            w = min(1.0, r + 0.5 - d)
            out = np.maximum(out, w * np.roll(np.roll(mask, dy, 0), dx, 1))
    return out


def shift(mask, dx, dy):
    return np.roll(np.roll(mask, dy, 0), dx, 1)


def load_font(s, size):
    font = ImageFont.truetype(os.path.join(FONTS, s["font"]), max(1, int(round(size))))
    if s.get("weight"):
        font.set_variation_by_axes([s["weight"]])
    return font


def draw_text(font, text, k):
    """Coverage of `text` alone (float, 0..1) and its ink box in that image"""
    bb = font.getbbox(text, anchor="ls")
    layer = Image.new("L", (bb[2] - bb[0] + 4 * k, bb[3] - bb[1] + 4 * k), 0)
    ImageDraw.Draw(layer).text((2 * k - bb[0], 2 * k - bb[1]), text, font=font, fill=255, anchor="ls")
    a = np.asarray(layer, dtype=np.float32) / 255.0
    ys, xs = np.nonzero(a > 0.02)
    return a, (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1), bb


def render_set(s):
    bpp = s.get("bpp", 4)
    analysed = []
    shared = None   # sets with "same_roles": the colours of the first glyph for all of the same palette
    for g in s["glyphs"]:
        img, base = original(g, bpp)
        top, bottom, edge = roles(img, base, s["edge"])
        if s.get("same_roles"):
            if shared and shared[0] == base:
                top, bottom, edge = shared[1]
            elif not shared:
                shared = (base, (top, bottom, edge))
        analysed.append((g, img, base, (top, bottom, edge), body_box(img, edge)))
    k = SCALE * SS
    box_fit = s.get("fit") == "box"
    if not box_fit:
        # One size for the whole set: capitals and digits as tall as the original's
        caps = [a for a in analysed if a[0][0][:1].isupper() or a[0][0][:1].isdigit()] or analysed
        cap_top = min(a[4][1] for a in caps)
        baseline = max(a[4][3] for a in caps)
        # "inset": the letters that much smaller (original pixels) on each
        # side, so their outline stays inside the tiles (it was cut below)
        cap_top += s.get("inset", 0)
        baseline -= s.get("inset", 0)
        cap_h = baseline - cap_top
        probe = load_font(s, 1000)
        ref = probe.getbbox("H", anchor="ls")   # ref[1]: top of the capitals, negative
        font = load_font(s, cap_h * k * 1000.0 / -ref[1])

    out = []
    for g, img, base, rl, box in analysed:
        th, tw = img.shape
        W, H = (tw + 2 * PAD) * k, (th + 2 * PAD) * k
        canvas = np.zeros((H, W), np.float32)
        if box_fit:
            # (keeping the word's outline inside its tiles: at least an
            # outline's width from their sides; "inset" shrinks it all round)
            i = s.get("inset", 0)
            room = s.get("outline", 0.85) + 0.3
            x0b, x1b = max(box[0] + i, room), min(box[2] - i, tw - room)
            box = (x0b, box[1] + i, x1b, box[3] - i)
        bw, bh = (box[2] - box[0]) * k, (box[3] - box[1]) * k
        if box_fit:
            # A word: its ink stretched over the original's box
            probe = load_font(s, 1000)
            ref = probe.getbbox("H", anchor="ls")
            font = load_font(s, bh * 1000.0 / -ref[1])
            a, ink, _ = draw_text(font, g[0], k)
            ink_img = Image.fromarray((a[ink[1]:ink[3], ink[0]:ink[2]] * 255).astype(np.uint8))
            glyph = np.asarray(ink_img.resize((max(1, int(bw)), max(1, int(bh))), Image.LANCZOS),
                               dtype=np.float32) / 255.0
            x0, y0 = int((PAD + box[0]) * k), int((PAD + box[1]) * k)
            canvas[y0:y0 + glyph.shape[0], x0:x0 + glyph.shape[1]] = glyph
            local_top, local_h = box[1], box[3] - box[1]
        else:
            # Letter drawn alone, then placed: same baseline for the set,
            # centred on the original letter and narrowed if wider than it
            a, ink, bb = draw_text(font, g[0], k)
            glyph = a
            gw = ink[2] - ink[0]
            sx = 1.0
            if gw > bw * s.get("max_width", 1.04):
                sx = bw * s.get("max_width", 1.04) / gw
            if sx != 1.0:
                nw = max(1, int(round(a.shape[1] * sx)))
                glyph = np.asarray(Image.fromarray((a * 255).astype(np.uint8)).resize((nw, a.shape[0]), Image.LANCZOS),
                                   dtype=np.float32) / 255.0
                ink = (ink[0] * sx, ink[1], ink[2] * sx, ink[3])
            cx = (PAD + (box[0] + box[2]) / 2.0) * k
            by = (PAD + baseline) * k
            x0 = int(round(cx - (ink[0] + ink[2]) / 2.0))
            y0 = int(round(by - (2 * k - bb[1])))   # the baseline of `a` is at 2k - bb[1]
            gh_, gw_ = glyph.shape
            ys0, xs0 = max(0, -y0), max(0, -x0)
            ys1, xs1 = min(gh_, H - y0), min(gw_, W - x0)
            if ys1 > ys0 and xs1 > xs0:
                canvas[y0 + ys0:y0 + ys1, x0 + xs0:x0 + xs1] = glyph[ys0:ys1, xs0:xs1]
            local_top, local_h = cap_top, cap_h
        # Down to atlas resolution, then the edge around it
        W, H = W // SS, H // SS
        letter = canvas.reshape(H, SS, W, SS).mean(axis=(1, 3))
        if s["edge"] == "outline":
            edge = dilate(letter, s.get("outline", 0.85) * SCALE)
        elif s["edge"] == "shadow":
            edge = shift(dilate(letter, 0.3 * SCALE), int(0.7 * SCALE), int(0.9 * SCALE))
        else:
            edge = np.zeros_like(letter)
        # Gradient position: 0 at the top of the capitals, 1 at the baseline
        ys = ((np.arange(H, dtype=np.float32) + 0.5) / SCALE - PAD - local_top) / max(local_h, 1)
        grad = np.repeat(np.clip(ys, 0, 1)[:, None], W, 1)
        # What falls in the margin around the glyph's tiles is cut on screen
        m = PAD * SCALE
        lost = edge.sum() - edge[m:H - m, m:W - m].sum()
        if lost > 0.002 * edge.sum():
            print(f"{s['name']} {g[0]!r}: {100 * lost / edge.sum():.0f}% of its edge outside its tiles")
        rgb = np.dstack([letter, edge, grad])
        out.append((g, rl, (rgb * 255 + 0.5).astype(np.uint8)))
    return out


# ---- Atlas -------------------------------------------------------------------

entries = {}   # (bpp, pattern or hash) -> (pattern, tile image, roles, glyph)
dupes = []     # 8 bpp tiles of the same pixels in two glyphs (the first one's drawing is used)
for s in SETS:
    bpp = s.get("bpp", 4)
    for g, rl, big in render_set(s):
        for ty, r in enumerate(g[1]):
            for tx, e in enumerate(r):
                if not any(pattern_bytes(e, bpp)):
                    continue   # an empty tile: the game shows nothing there
                # 8 bpp tiles are found by their pixels alone (the game loads
                # them at other numbers in each race): one per picture
                key = (bpp, pattern_hash(e, bpp) if bpp == 8 else pattern_number(e, bpp))
                if key in entries:
                    if bpp == 8 and entries[key][3] != (s["name"], g[0]):
                        dupes.append((entries[key][3], (s["name"], g[0])))
                    continue
                x, y = tx * 8 * SCALE, ty * 8 * SCALE
                entries[key] = (pattern_number(e, bpp), big[y:y + SLOT, x:x + SLOT], rl, (s["name"], g[0]))

# ---- Blocks (glyphs.txt "G8") --------------------------------------------------
# Sega Rally 2's position ("16th") and time left are drawn with tiles made
# for each value (the game composes them, overlapping digits sharing tiles),
# so each value is a block found whole on screen, by the pixels of its cells.
# Their tiles come from snapshots of the race (out/snap): the positions were
# forced with SR2_POKE=22D018=<position - 1> (the position shown), the time
# left read as it counted down. A block: text parts (string, relative size)
# drawn side by side on one baseline and stretched over the original's box.
vrams = {}


def use_snapshot(path):
    global vram
    if path not in vrams:
        vrams[path] = open(path, "rb").read()[-VRAM_SIZE:]
    vram = vrams[path]


def name_entry(col, row_):
    w = struct.unpack_from("<I", vram, 0xF8000 + (row_ * 32 + col // 2) * 4)[0]   # layer A
    return (w & 0xFFFF) if col & 1 else (w >> 16)


SNAP = "out/snap/snap_%d.bin"
position_snaps = {1: 2300, 2: 2881, 3: 2841, 4: 2902, 5: 2962, 6: 3022, 7: 3082, 8: 3142, 9: 3202, 10: 3262,
                  11: 3322, 12: 3382, 13: 3442, 14: 3502, 15: 3562, 16: 3623}


def ordinal(n):
    return "th" if 10 <= n % 100 <= 20 else {1: "st", 2: "nd", 3: "rd"}.get(n % 10, "th")


BLOCKS = []
for n, snap_n in position_snaps.items():
    BLOCKS.append({"set": "position", "parts": [(str(n), 1.0), (ordinal(n), 0.5)], "snap": SNAP % snap_n,
                   "at": (46, 4), "size": (9, 5)})
    if n == 1:   # first place is gold
        BLOCKS[-1]["targets"] = [(255, 255, 150), (230, 130, 0), (0, 0, 0)]
BLOCKS.append({"set": "position", "parts": [("/", 1.0), ("16", 0.62)], "snap": SNAP % 2271, "at": (55, 4),
               "size": (5, 5)})
BLOCKS.append({"set": "position", "parts": [("/", 1.0), (" 2", 0.62)], "snap": SNAP % 3701, "at": (55, 4),
               "size": (5, 5)})
# The time left: a 4x4 block each digit (snapshot, column)
for d, (snap_n, col) in {0: (3560, 31), 1: (3520, 31), 2: (3435, 31), 3: (3371, 31), 4: (3306, 27), 5: (2890, 27),
                         6: (3207, 31), 7: (3143, 31), 8: (3080, 31), 9: (3016, 31)}.items():
    BLOCKS.append({"set": "timeleft", "parts": [(str(d), 1.0)], "snap": SNAP % snap_n, "at": (col, 4),
                   "size": (4, 4)})
# The car's name and DRIVER, bottom right: one picture each car (row 44,
# columns 46-59; the names share some tiles, so whole blocks), DRIVER on the
# next row (the red dots stay the game's)
CAR_SNAPS = {"PEUGEOT 306": "out/snap_coches/coche_0.bin", "SUBARU IMPREZA": "out/snap_coches/coche_3.bin",
             "MITSUBISHI LANCER": "out/snap_coches/coche_4.bin", "LANCIA STRATOS": "out/snap_coches/coche_5.bin",
             "TOYOTA CELICA": "out/snap_coches/coche_6.bin", "LANCIA DELTA": "out/snap_coches/coche_7.bin",
             "TOYOTA COROLLA": SNAP % 2271, "FORD ESCORT": SNAP % 3016}
for car, path in CAR_SNAPS.items():
    BLOCKS.append({"set": "carname", "parts": [(car, 1.0)], "snap": path, "at": (46, 44), "size": (14, 1)})
BLOCKS.append({"set": "carname", "parts": [("DRIVER", 1.0)], "snap": SNAP % 2271, "at": (49, 45), "size": (5, 1)})
# The yellow LCD speed of the Impreza and the Delta: segment pieces shared
# between digits, a 2x3 block each (snapshot, column; row 43), and its km/h
LCD = "out/snap_lcd/snap_%d.bin"
for d, (snap_n, col) in {"2": (2120, 15), "0": (2120, 17), "1": (2120, 19), "9": (2125, 17), "8": (2125, 19),
                         "4": (2130, 19), "7": (2134, 17), "3": (2134, 19), "6": (2138, 17), "5": (2143, 17)}.items():
    BLOCKS.append({"set": "lcd", "parts": [(d, 1.0)], "snap": LCD % snap_n, "at": (col, 43), "size": (2, 3)})
BLOCKS.append({"set": "lcd_units", "parts": [("km/h", 1.0)], "snap": LCD % 2120, "at": (21, 43), "size": (4, 3)})
# The Delta's LCD: the same pieces in the same tiles (its digits and km/h use
# the Impreza's name entries), but its own grey pixels: the layout from the
# Impreza's snapshots, the pixels and the palette from a Delta race
DELTA = "out/snap_coches/coche_7.bin"
BLOCKS += [dict(b, pixels=DELTA, targets=[(255, 255, 255), (190, 185, 200), (0, 0, 0)])
           for b in BLOCKS if b["set"] in ("lcd", "lcd_units")]
BLOCK_SETS = {
    "lcd": {"seven_segment": True, "edge": "outline", "outline": 0.8, "room": 1.0,
            "targets": [(255, 255, 200), (230, 220, 110), (0, 0, 0)]},
    "lcd_units": {"font": "Exo2-Italic[wght].ttf", "weight": 600, "edge": "outline", "outline": 0.8, "room": 1.0,
                  "targets": [(255, 255, 200), (230, 220, 110), (0, 0, 0)]},
    "carname": {"font": "Exo2[wght].ttf", "weight": 800, "edge": "shadow", "outline": 0.3, "room": 0.6,
                "tracking": 0.12, "targets": [(255, 255, 255), (150, 160, 230), (40, 40, 90)]},
    # targets: the gradient's top and bottom and the edge, as RGB
    "position": {"font": "Exo2-Italic[wght].ttf", "weight": 800, "edge": "outline", "outline": 1.2, "room": 1.4,
                 "targets": [(255, 255, 230), (140, 150, 230), (0, 0, 0)]},
    "timeleft": {"font": "Exo2-Italic[wght].ttf", "weight": 900, "edge": "extrude", "outline": 0.6, "room": 1.6,
                 "targets": [(255, 255, 255), (170, 170, 185), (170, 110, 0)]},
}


SEGMENTS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
            "7": "abc", "8": "abcdefg", "9": "abcdfg"}


def seven_segment(digit, w=200, h=340, t=34, gap=6, slant=0.14):
    """An italic seven-segment digit (L image): segments a (top) .. g (middle)"""
    img = Image.new("L", (int(w + slant * h + 40), h + 40), 0)
    d = ImageDraw.Draw(img)
    ox, oy, m = 20, 20, h / 2

    def bar(x0, y0, x1, y1):   # a segment between two points, its ends pointed
        if y0 == y1:   # horizontal
            pts = [(x0 + gap, y0), (x0 + gap + t / 2, y0 - t / 2), (x1 - gap - t / 2, y0 - t / 2), (x1 - gap, y0),
                   (x1 - gap - t / 2, y0 + t / 2), (x0 + gap + t / 2, y0 + t / 2)]
        else:          # vertical
            pts = [(x0, y0 + gap), (x0 + t / 2, y0 + gap + t / 2), (x0 + t / 2, y1 - gap - t / 2), (x0, y1 - gap),
                   (x0 - t / 2, y1 - gap - t / 2), (x0 - t / 2, y0 + gap + t / 2)]
        d.polygon([(ox + x + slant * (h - y), oy + y) for x, y in pts], fill=255)

    L, R = t / 2, w - t / 2
    T, B = t / 2, h - t / 2
    seg = {"a": (L, T, R, T), "g": (L, m, R, m), "d": (L, B, R, B), "f": (L, T, L, m), "b": (R, T, R, m),
           "e": (L, m, L, B), "c": (R, m, R, B)}
    for s in SEGMENTS[digit]:
        bar(*seg[s])
    return img


def render_block(b):
    st = BLOCK_SETS[b["set"]]
    use_snapshot(b["snap"])
    c0, r0 = b["at"]
    w, h = b["size"]
    grid_ = [[name_entry(c0 + x, r0 + y) for x in range(w)] for y in range(h)]
    if "pixels" in b:   # the tiles' pixels (and palette) from another snapshot
        use_snapshot(b["pixels"])
    blank = [[not any(pattern_bytes(e, 8)) for e in r] for r in grid_]
    img = np.vstack([np.hstack([pattern(e, 8) for e in r]) for r in grid_])
    first = next(e for r, br in zip(grid_, blank) for e, bl in zip(r, br) if not bl)
    base = entry_base(first, 8)
    # The colours: the ones of the block nearest to the set's targets (the
    # tiles of one block can sit in banks whose palettes are ordered alike
    # in colour but not in index)
    used = [int(n) for n in np.unique(img) if n]
    counts = {n: int((img == n).sum()) for n in used}
    common = [n for n in used if counts[n] * 40 >= img.size // 4] or used

    def nearest(target):
        rgb = lambda n: np.array(palette_rgb(base | n), np.float32) * 255 / 31
        return min(common, key=lambda n: float(np.sum((rgb(n) - np.array(target, np.float32)) ** 2)))

    rl = tuple(nearest(t) for t in b.get("targets", st["targets"]))
    box = body_box(img, rl[2])
    # the parts on one baseline, then the ink stretched over the box (an
    # outline's width inside the block's tiles)
    room = st["room"]
    x0b, y0b = max(box[0], room), max(box[1], room)
    x1b, y1b = min(box[2], w * 8 - room), min(box[3], h * 8 - room)
    size = 400
    parts = b["parts"] if not st.get("seven_segment") else []
    if st.get("tracking"):   # letters spread apart, as the game's spaced capitals: one part a letter
        parts = [(ch, sc) for t, sc in parts for ch in t]
    font_parts = [(t, load_font(st, size * sc)) for t, sc in parts]
    gap = st.get("tracking", 0.06) * size
    widths = [f.getbbox(t, anchor="ls") if t != " " else (0, 0, int(f.getlength(" ")), 0) for t, f in font_parts]
    total = sum(bb[2] - bb[0] for bb in widths) + gap * max(len(widths) - 1, 0)
    layer = Image.new("L", (int(total + size), int(size * 2)), 0)
    d = ImageDraw.Draw(layer)
    x = size * 0.5
    for (t, f), bb in zip(font_parts, widths):
        if t != " ":
            d.text((x - bb[0], size * 1.5), t, font=f, fill=255, anchor="ls")
        x += bb[2] - bb[0] + gap
    if st.get("seven_segment"):   # an LCD digit: its segments drawn as slanted bars
        layer = seven_segment(b["parts"][0][0])
    a = np.asarray(layer, dtype=np.float32) / 255.0
    ys, xs = np.nonzero(a > 0.02)
    ink = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    bw, bh = int((x1b - x0b) * SCALE), int((y1b - y0b) * SCALE)
    glyph = np.asarray(Image.fromarray((ink * 255).astype(np.uint8)).resize((bw, bh), Image.LANCZOS),
                       dtype=np.float32) / 255.0
    W, H = (w * 8 + 2 * PAD) * SCALE, (h * 8 + 2 * PAD) * SCALE
    letter = np.zeros((H, W), np.float32)
    gx, gy = int(round((PAD + x0b) * SCALE)), int(round((PAD + y0b) * SCALE))
    letter[gy:gy + bh, gx:gx + bw] = glyph
    if st["edge"] == "extrude":   # a thin rim and the digit's side, down and to the right
        rim = dilate(letter, st["outline"] * SCALE)
        side = np.maximum.reduce([shift(rim, int(t * 0.8 * SCALE), int(t * SCALE)) for t in (0.4, 0.8, 1.2)])
        edge = np.maximum(rim, side)
    elif st["edge"] == "shadow":
        edge = shift(dilate(letter, 0.3 * SCALE), int(0.7 * SCALE), int(0.9 * SCALE))
    else:
        edge = dilate(letter, st["outline"] * SCALE)
    ys_ = ((np.arange(H, dtype=np.float32) + 0.5) / SCALE - PAD - y0b) / max(y1b - y0b, 1)
    grad = np.repeat(np.clip(ys_, 0, 1)[:, None], W, 1)
    m = PAD * SCALE
    lost = edge.sum() - edge[m:H - m, m:W - m].sum()
    if lost > 0.002 * edge.sum():
        print(f"{b['set']} {b['parts']}: {100 * lost / edge.sum():.0f}% of its edge outside its tiles")
    big = (np.dstack([letter, edge, grad]) * 255 + 0.5).astype(np.uint8)
    hashes, tiles = [], []
    for ty in range(h):
        for tx in range(w):
            hashes.append(0 if blank[ty][tx] else pattern_hash(grid_[ty][tx], 8))
            tiles.append(big[ty * 8 * SCALE:ty * 8 * SCALE + SLOT, tx * 8 * SCALE:tx * 8 * SCALE + SLOT])
    return w, h, rl, hashes, tiles, img, base, b.get("pixels", b["snap"])


blocks = [render_block(b) for b in BLOCKS]
use_snapshot(snap)

rows = (len(entries) + sum(len(b[4]) for b in blocks) + COLS - 1) // COLS
atlas = np.zeros((rows * SLOT, COLS * SLOT, 4), np.uint8)
atlas[..., 3] = 255
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "glyphs.txt"), "w") as f:
    f.write(f"# High-resolution text glyphs made from {snap}\n")
    f.write("# [B] pattern hash slot top bottom edge (B: 8 bpp)\n")
    for slot, ((bpp, key), (p, tile, rl, _)) in enumerate(sorted(entries.items(), key=lambda kv: (kv[0][0], kv[1][0]))):
        sy, sx = divmod(slot, COLS)
        atlas[sy * SLOT:(sy + 1) * SLOT, sx * SLOT:(sx + 1) * SLOT, :3] = tile
        h = key if bpp == 8 else lwhash.fnv1a_bytes(vram[p * 32:p * 32 + 32])
        f.write(f"{'B ' if bpp == 8 else ''}{p:04x} {h:016x} {slot} {rl[0]:x} {rl[1]:x} {rl[2]:x}\n")
    # Blocks: G8 width height top bottom edge, then per cell hash:slot (hash 0: empty)
    slot = len(entries)
    for w, h, rl, hashes, tiles, _, _, _ in blocks:
        cells = []
        for hh, tile in zip(hashes, tiles):
            sy, sx = divmod(slot, COLS)
            atlas[sy * SLOT:(sy + 1) * SLOT, sx * SLOT:(sx + 1) * SLOT, :3] = tile
            cells.append(f"{hh:016x}:{slot}")
            slot += 1
        f.write(f"G8 {w} {h} {rl[0]:x} {rl[1]:x} {rl[2]:x} " + " ".join(cells) + "\n")
for a, b in dupes:
    print(f"same tile in {a} and {b}: drawn as in the first")
Image.fromarray(atlas, "RGBA").save(os.path.join(OUT, "atlas.png"))
print(f"{OUT}/atlas.png: {len(entries)} patterns, {COLS * SLOT}x{rows * SLOT}")

if preview:
    # Every set as the game would show it, coloured with the snapshot's
    # palette, the original pixel art above it
    def colour(base, n):
        r, g, b = palette_rgb(base | n)
        return np.array([r * 255 // 31, g * 255 // 31, b * 255 // 31], np.float32)

    lines = []
    for s in SETS:
        bpp = s.get("bpp", 4)
        rendered = render_set(s)
        width = sum(len(g[1][0]) * 8 * SCALE for g, _, _ in rendered)
        height = max(len(g[1]) * 8 * SCALE for g, _, _ in rendered)
        orig = np.zeros((height, width, 3), np.float32) + np.array([30, 34, 70], np.float32)
        line = orig.copy()
        x = 0
        for g, rl, big in rendered:
            img, base = original(g, bpp)
            th, tw = len(g[1]) * 8 * SCALE, len(g[1][0]) * 8 * SCALE
            for yy in range(img.shape[0]):
                for xx in range(img.shape[1]):
                    if img[yy, xx]:
                        orig[yy * SCALE:(yy + 1) * SCALE, x + xx * SCALE:x + (xx + 1) * SCALE] = colour(base, img[yy, xx])
            px = big[PAD * SCALE:PAD * SCALE + th, PAD * SCALE:PAD * SCALE + tw].astype(np.float32) / 255.0
            top, bottom, edge = (colour(base, n) for n in rl)
            fill = top * (1 - px[..., 2:3]) + bottom * px[..., 2:3]
            region = line[:th, x:x + tw]
            if rl[2]:
                region[:] = region * (1 - px[..., 1:2]) + edge * px[..., 1:2]
            region[:] = region * (1 - px[..., 0:1]) + fill * px[..., 0:1]
            x += tw
        lines += [orig, line]
    # ... and the blocks
    for bw_, bh_, rl, _, tiles, img, base, pixels in blocks:
        use_snapshot(pixels)
        th, tw = bh_ * 8 * SCALE, bw_ * 8 * SCALE
        orig = np.zeros((th, tw, 3), np.float32) + np.array([30, 34, 70], np.float32)
        line = orig.copy()
        for yy in range(img.shape[0]):
            for xx in range(img.shape[1]):
                if img[yy, xx]:
                    orig[yy * SCALE:(yy + 1) * SCALE, xx * SCALE:(xx + 1) * SCALE] = colour(base, img[yy, xx])
        big = np.zeros((th, tw, 3), np.float32)
        for k, t in enumerate(tiles):
            ty, tx = divmod(k, bw_)
            big[ty * 8 * SCALE:(ty + 1) * 8 * SCALE, tx * 8 * SCALE:(tx + 1) * 8 * SCALE] = \
                t[PAD * SCALE:PAD * SCALE + 8 * SCALE, PAD * SCALE:PAD * SCALE + 8 * SCALE] / 255.0
        top, bottom, edge = (colour(base, n) for n in rl)
        fill = top * (1 - big[..., 2:3]) + bottom * big[..., 2:3]
        line = line * (1 - big[..., 1:2]) + edge * big[..., 1:2]
        line = line * (1 - big[..., 0:1]) + fill * big[..., 0:1]
        lines += [np.hstack([orig, line])]
    width = max(l.shape[1] for l in lines)
    sheet = np.vstack([np.pad(l, ((4, 4), (0, width - l.shape[1]), (0, 0))) for l in lines])
    Image.fromarray(sheet.clip(0, 255).astype(np.uint8), "RGB").save(preview)
    print("Preview:", preview)
