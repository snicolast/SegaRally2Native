"""High-resolution pictures of the bar graph rev counters (Impreza, Delta),
off and lit, drawn as vectors over the original's own geometry: the bars,
the band, the numbers and the ticks are measured from the captures
(out/2d/barras_coche_N_ralenti / _bajo, see make_bar_tachs.py) and redrawn
16 times larger with smooth edges. Writes textures/art/barras_<car>.png,
barras_<car>_lit.png and barras_<car>_seg.png (the whole counter's box; _seg:
each changing pixel's bar or segment, numbered in the order they light up, in
red, so the fill is cut along their edges, see src/video/pack2d.h), which

  python tools/make_bar_tachs.py split

cuts into the boxes the game uses.

  python tools/make_bar_art.py
Needs Pillow and numpy.
"""
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 16           # picture pixels per 2D pixel
SS = 2           # supersampling while drawing
K = S * SS
FONT = "third_party/fonts/Arimo[wght].ttf"


def load(n, state):
    return np.array(Image.open(f"out/2d/barras_coche_{n}_{state}.png").convert("RGBA")).astype(int)


def font(px):
    f = ImageFont.truetype(FONT, int(px))
    try:
        f.set_variation_by_axes([700])
    except Exception:
        pass
    return f


def fit_circle(pts):
    p = np.array(pts, float)
    x, y = p[:, 0], p[:, 1]
    a = np.c_[2 * x, 2 * y, np.ones(len(x))]
    cx, cy, c = np.linalg.lstsq(a, x * x + y * y, rcond=None)[0]
    return cx, cy, math.sqrt(c + cx * cx + cy * cy)


def finish(img):
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def pieces(mask):
    """The 4-connected pieces of a boolean mask: [(ys, xs), ...]"""
    seen = np.zeros(mask.shape, bool)
    out = []
    for y0, x0 in zip(*np.nonzero(mask)):
        if seen[y0, x0]:
            continue
        seen[y0, x0] = True
        todo, ys, xs = [(y0, x0)], [], []
        while todo:
            y, x = todo.pop()
            ys.append(y)
            xs.append(x)
            for yy, xx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= yy < mask.shape[0] and 0 <= xx < mask.shape[1] and mask[yy, xx] and not seen[yy, xx]:
                    seen[yy, xx] = True
                    todo.append((yy, xx))
        out.append((np.array(ys), np.array(xs)))
    return out


def segment_map(pics, number):
    """A picture the size of pics' whose red is number(x, y) at each pixel's
    centre (x, y in 2D pixels of the box), where the off and lit pictures
    differ (and half a 2D pixel around): 0 elsewhere"""
    w, h = pics[False].size
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    n = number((xs + 0.5) / S, (ys + 0.5) / S).astype(np.uint8)
    a, b = (np.asarray(pics[k], dtype=np.int16) for k in (False, True))
    differ = Image.fromarray(((np.abs(a - b).max(-1) > 6) * 255).astype(np.uint8))
    n[np.asarray(differ.filter(ImageFilter.MaxFilter(S + 1))) == 0] = 0
    out = np.zeros((h, w, 4), np.uint8)
    out[..., 0] = n
    out[..., 3] = 255
    return Image.fromarray(out, "RGBA")


def text(d, xy, s, size, fill, strokes):
    """Text centred on xy (2D pixels, picture origin), with outlines from the
    outside in: [(width in 2D pixels, colour), ...]"""
    f = font(size * K)
    x, y = xy[0] * K, xy[1] * K
    for w, c in strokes:
        d.text((x, y), s, font=f, fill=c, anchor="mm", stroke_width=int(w * K), stroke_fill=c)
    d.text((x, y), s, font=f, fill=fill, anchor="mm")


# ---- Impreza: 25 yellow bars on a rising arc, in a green outlined band ------

def impreza():
    box = (2, 33, 14, 13)
    ox, oy = box[0] * 8, box[1] * 8
    off, on = load(3, "ralenti"), load(3, "bajo")
    diff = (off != on).any(-1)
    region = diff[oy:oy + box[3] * 8, ox:ox + box[2] * 8]
    cols = region.any(0)
    bars, x = [], 0
    while x < len(cols):
        if not cols[x]:
            x += 1
            continue
        s = x
        while x < len(cols) and cols[x]:
            x += 1
        ys = np.where(region[:, s:x].any(1))[0]
        colour = tuple(on[oy + (ys.min() + ys.max()) // 2, ox + (s + x - 1) // 2][:3])
        bars.append((s, x, ys.min(), ys.max() + 1, colour))   # x0, x1 (end), top, bottom (end), lit colour
    # One more bar after them, never lit in the captures (the top revs): an
    # empty slot as wide as the last bar, a black pixel after it. Its colour
    # is not known; the last one's
    x0, x1, _, _, colour = bars[-1]
    bars.append((x1 + 1, x1 + 1 + (x1 - x0), None, None, colour))
    top = fit_circle([((b[0] + b[1]) / 2, b[2]) for b in bars[:-1]])
    bottom = fit_circle([((b[0] + b[1]) / 2, b[3]) for b in bars[:-1]])

    # the arcs bulge upwards: points above the centre
    def ytop(x, grow=0.0):
        cx, cy, r = top
        return cy - math.sqrt(max((r + grow) ** 2 - (x - cx) ** 2, 0.0))

    def ybot(x, grow=0.0):
        cx, cy, r = bottom
        return cy - math.sqrt(max((r - grow) ** 2 - (x - cx) ** 2, 0.0))

    x_first, x_last = bars[0][0], bars[-1][1]

    def band(grow):
        xs = np.linspace(x_first - grow, x_last + grow, 160)
        upper = [(x * K, ytop(x, grow) * K) for x in xs]
        lower = [(x * K, ybot(x, grow) * K) for x in xs[::-1]]
        return upper + lower

    W, H = box[2] * 8 * K, box[3] * 8 * K
    green_dark, green_light = (0x41, 0x7B, 0x39, 255), (0xA4, 0xD5, 0x9C, 255)
    numbers = [("3", (4.5, 98.5)), ("4", (16.5, 82.5)), ("5", (32.5, 69.5)), ("6", (54.5, 57.5)),
               ("7", (77.5, 51.5)), ("8", (104.5, 49.5))]
    pics = {}
    for lit in (False, True):
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        # badges of the numbers first, so the band's outline runs over their joins
        for s, (x, y) in numbers:
            text(d, (x, y), s, 8.6, (0, 0, 0, 255), [(2.6, green_dark), (1.6, green_light)])
        text(d, (96.5, 60.5), "RPM", 8.4, (0, 0, 0, 255), [(2.6, green_dark), (1.6, green_light)])
        d.polygon(band(3.0), fill=green_dark)
        d.polygon(band(2.0), fill=green_light)
        d.polygon(band(1.0), fill=(0, 0, 0, 255))
        for i, (x0, x1, _, _, colour) in enumerate(bars):
            gap = 0.5   # half the black between two bars
            a, b = x0 + gap * (i > 0) - 0.15, x1 - gap * (i + 1 < len(bars)) + 0.15
            quad = [(a * K, ytop(a) * K), (b * K, ytop(b) * K), (b * K, ybot(b) * K), (a * K, ybot(a) * K)]
            if lit:
                d.polygon(quad, fill=colour + (255,))
                # a soft highlight down the left side of each bar
                hl = tuple(min(255, c + 40) for c in colour) + (255,)
                c0 = a + (b - a) * 0.12
                c1 = a + (b - a) * 0.38
                d.polygon([(c0 * K, ytop(c0) * K + K * 0.6), (c1 * K, ytop(c1) * K + K * 0.6),
                           (c1 * K, ybot(c1) * K - K * 0.6), (c0 * K, ybot(c0) * K - K * 0.6)], fill=hl)
            else:
                d.polygon(quad, fill=(0, 0, 0, 0))   # off: the scene shows through, as in the arcade
        pics[lit] = finish(img)
    # the bars, numbered from the left: each up to the middle of the black after it
    ends = [(bars[i][1] + bars[i + 1][0]) / 2 for i in range(len(bars) - 1)]
    pics["seg"] = segment_map(pics, lambda x, y: 1 + sum((x >= e).astype(np.int32) for e in ends))
    return pics


# ---- Delta: a pale band on an arc that fills with 24 dark segments -------------

def delta():
    box = (2, 36, 16, 9)
    ox, oy = box[0] * 8, box[1] * 8
    off, on = load(7, "ralenti"), load(7, "bajo")
    cx, cy = 96.28 - ox, 384.9 - oy      # the band's centre (fitted on the captures' edges)
    r_out, r_in = 94.4, 84.6
    a_left, a_right = 147.5, 65.0        # degrees, anticlockwise from the right
    # The segments as the game draws them: the dark pieces of the lit capture
    # (24), each at its mean angle; a segment ends halfway to the next one
    dark = ((on[..., 0] == 0x18) & (on[..., 1] == 0x18) & (on[..., 2] == 0x10) & (off != on).any(-1))
    centres = sorted((math.degrees(math.atan2(cy - (ys.mean() + 0.5), xs.mean() + 0.5 - cx))
                      for ys, xs in pieces(dark[oy:oy + box[3] * 8, ox:ox + box[2] * 8])), reverse=True)
    bounds = [a_left] + [(a + b) / 2 for a, b in zip(centres, centres[1:])] + [a_right]
    segments = len(centres)

    def pt(r, a):
        return ((cx + r * math.cos(math.radians(a))) * K, (cy - r * math.sin(math.radians(a))) * K)

    def ring(r0, r1, a0, a1, n=120):
        outer = [pt(r1, a0 + (a1 - a0) * i / n) for i in range(n + 1)]
        inner = [pt(r0, a1 + (a0 - a1) * i / n) for i in range(n + 1)]
        return outer + inner

    numbers = [("1", (36, 349)), ("2", (42, 340)), ("3", (49, 333)), ("4", (61.5, 324)), ("5", (78, 319)),
               ("6", (95, 317)), ("7", (110, 318)), ("8", (125, 321))]
    angles = [math.degrees(math.atan2(cy + oy - y, x - cx - ox)) for _, (x, y) in numbers]
    W, H = box[2] * 8 * K, box[3] * 8 * K
    pics = {}
    for lit in (False, True):
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        # rim: light grey outside, dark grey line, then the band
        d.polygon(ring(r_in - 0.9, r_out + 1.6, a_left + 0.5, a_right - 0.5), fill=(0xBD, 0xBD, 0xBD, 255))
        d.polygon(ring(r_in - 0.6, r_out + 0.7, a_left + 0.3, a_right - 0.3), fill=(0x33, 0x33, 0x33, 255))
        if lit:
            d.polygon(ring(r_in, r_out, a_left, a_right), fill=(0x52, 0x52, 0x52, 255))
            for i in range(segments):
                a0, a1 = bounds[i] - 0.28, bounds[i + 1] + 0.28
                d.polygon(ring(r_in + 0.35, r_out - 0.35, a0, a1, 12), fill=(0x18, 0x18, 0x10, 255))
                # a faint sheen on the outer half
                d.polygon(ring(r_out - 3.2, r_out - 0.35, a0, a1, 12), fill=(0x29, 0x29, 0x22, 255))
        else:
            steps = 24   # pale green, a little darker towards the outside
            for i in range(steps):
                t = i / (steps - 1)
                r1 = r_out - (r_out - r_in) * i / steps
                c0, c1 = np.array([0xB4, 0xC5, 0x8B]), np.array([0xEE, 0xFF, 0xB4])
                c = tuple(int(v) for v in c0 + (c1 - c0) * min(1.0, t * 1.8)) + (255,)
                d.polygon(ring(r_in, r1, a_left, a_right), fill=c)
        # ticks inside the band: long at the numbers, short between them
        for i, a in enumerate(angles):
            d.line([pt(r_in - 0.2, a), pt(r_in - 3.6, a)], fill=(0xEE, 0xEE, 0xEE, 255), width=int(1.1 * K))
            if i + 1 < len(angles):
                m = (a + angles[i + 1]) / 2
                d.line([pt(r_in - 0.2, m), pt(r_in - 2.2, m)], fill=(0xCC, 0xCC, 0xCC, 255), width=int(0.8 * K))
        for s, (x, y) in numbers:
            text(d, (x - ox, y - oy), s, 8.8, (255, 255, 255, 255), [(1.1, (0, 0, 0, 255))])
        pics[lit] = finish(img)
    # the segments, numbered from the left end: each between two of the gaps' middles
    def number(x, y):
        a = np.degrees(np.arctan2(cy - y, x - cx))
        return 1 + sum((a < b).astype(np.int32) for b in bounds[1:-1])
    pics["seg"] = segment_map(pics, number)
    return pics


if __name__ == "__main__":
    for car, make in (("impreza", impreza), ("delta", delta)):
        pics = make()
        pics[False].save(f"textures/art/barras_{car}.png")
        pics[True].save(f"textures/art/barras_{car}_lit.png")
        pics["seg"].save(f"textures/art/barras_{car}_seg.png", optimize=True)
        print(car, pics[False].size)
