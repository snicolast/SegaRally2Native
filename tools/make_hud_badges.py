"""The small badges of Sega Rally 2's race HUD as 2D replacements
(src/video/pack2d.h), redrawn as vectors 16 times larger:
  vuelta_1..5   the lap numbers left of the lap times (silver box, red to
                orange digit with a black shadow)
  top3          the 1 2 3 boxes of STAGE TOP 3 (metal boxes, pale yellow
                digits outlined in dark grey)

  python tools/make_hud_badges.py out/2d/capture_N
      textures/2d/vuelta_N.txt / top3.txt from a race capture (F12 in the
      game, or the 2D layers of an F9 snapshot) that shows all five laps
      and STAGE TOP 3, and the pictures textures/2d/vuelta_N.png / top3.png
Needs Pillow and numpy.
"""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S, SS = 16, 4          # picture pixels per 2D pixel; supersampling while drawing
K = S * SS
FONT = "third_party/fonts/ArchivoBlack-Regular.ttf"
LAPS = [(2, 9 + 3 * i, 2, 3) for i in range(5)]   # cells: x, y, w, h (24 rows each, from y 72)
TOP3 = (46, 10, 2, 5)                             # the three boxes, 9 rows each from y 86, x 370..380


def write_txt(name, cells, box, source):
    x, y, w, h = box
    with open(f"textures/2d/{name}.txt", "w") as f:
        f.write(f"# race HUD badge, from {source}\nrect {x} {y} {w} {h}\n")
        for r in range(y, y + h):
            for c in range(x, x + w):
                f.write(cells[r][c] + "\n")


def gradient(d, box, stops):
    """Vertical gradient over box (picture pixels), stops [(t, (r, g, b)), ...]"""
    x0, y0, x1, y1 = box
    for y in range(int(y0), int(y1)):
        t = min(max((y - y0) / max(y1 - y0 - 1, 1), 0.0), 1.0)
        for (ta, ca), (tb, cb) in zip(stops, stops[1:]):
            if ta <= t <= tb:
                k = (t - ta) / max(tb - ta, 1e-6)
                c = tuple(int(a + (b - a) * k) for a, b in zip(ca, cb))
                break
        d.line([(x0, y), (x1 - 1, y)], fill=c + (255,))


def digit_layer(size, ch, box, fill_stops, font_px):
    """The digit fitted into box (2D pixels in the picture), filled with a
    vertical gradient: an RGBA layer and its mask"""
    font = ImageFont.truetype(FONT, int(font_px * K))
    mask = Image.new("L", size, 0)
    md = ImageDraw.Draw(mask)
    x0, y0, x1, y1 = (v * K for v in box)
    glyph = Image.new("L", (int(font_px * K * 2), int(font_px * K * 2)), 0)
    ImageDraw.Draw(glyph).text((font_px * K * 0.3, font_px * K * 0.2), ch, font=font, fill=255)
    glyph = glyph.crop(glyph.getbbox()).resize((int(x1 - x0), int(y1 - y0)), Image.LANCZOS)
    mask.paste(glyph, (int(x0), int(y0)))
    colour = Image.new("RGBA", size, (0, 0, 0, 0))
    gradient(ImageDraw.Draw(colour), (0, y0, size[0], y1), fill_stops)
    return colour, mask


def lap_badge(n):
    w, h = 16, 24
    size = (w * K, h * K)
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # black edge right and below (x 14, row 23), grey edge left and on top
    d.rectangle([0, 0, 15 * K - 1, 24 * K - 1], fill=(0, 0, 0, 255))
    d.rectangle([0, 0, 14 * K - 1, 23 * K - 1], fill=(0x52, 0x52, 0x52, 255))
    gradient(d, (1 * K, 1 * K, 14 * K, 23 * K),
             [(0.0, (255, 255, 255)), (0.18, (255, 255, 255)), (0.4, (0xDE, 0xDE, 0xDE)),
              (0.75, (0xA4, 0xA4, 0xA4)), (1.0, (0x73, 0x73, 0x73))])
    # soft shine on the upper half
    shine = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(shine).rectangle([1 * K, 1 * K, 14 * K - 1, int(10.5 * K)], fill=(255, 255, 255, 40))
    img.alpha_composite(shine)
    box = (3.6, 5.6, 11.4, 18.4)   # the digit, as the original's (x 19..25, rows 78..89 for the 1)
    fill, mask = digit_layer(size, str(n), box,
                             [(0.0, (0xF6, 0x31, 0x10)), (0.35, (0xF6, 0x4A, 0x10)), (0.7, (0xFF, 0x83, 0x00)),
                              (1.0, (0xFF, 0xA4, 0x00))], 14)
    shadow = Image.new("RGBA", size, (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 255), (int(0.9 * K), int(0.9 * K)), mask)
    img.alpha_composite(shadow)
    edge = Image.new("RGBA", size, (0, 0, 0, 0))
    edge.paste((0x60, 0x10, 0x00, 255), (0, 0), mask.filter(ImageFilter.MaxFilter(5)))
    img.alpha_composite(edge)
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.paste(fill, (0, 0), mask)
    img.alpha_composite(out)
    return img.resize((w * S, h * S), Image.LANCZOS)


def top3_badges():
    w, h = 16, 40   # cells 46..47 x 10..14: x 368..383, y 80..119
    size = (w * K, h * K)
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i in range(3):
        x0, y0 = 2, 6 + 9 * i          # 370, 86 + 9 i: the box with its black frame, 11 x 10
        d.rounded_rectangle([x0 * K, y0 * K, (x0 + 11) * K - 1, (y0 + 10) * K - 1], radius=int(0.8 * K),
                            fill=(0, 0, 0, 255))
        gradient(d, ((x0 + 1) * K, (y0 + 1) * K, (x0 + 10) * K, (y0 + 9) * K),
                 [(0.0, (0x8B, 0x8B, 0x8B)), (0.3, (0xE6, 0xE6, 0xE6)), (0.55, (0xB4, 0xB4, 0xB4)),
                  (1.0, (0x73, 0x73, 0x73))])
        d.rectangle([(x0 + 2) * K, (y0 + 1) * K, (x0 + 9) * K - 1, (y0 + 9) * K - 1], fill=(0x29, 0x29, 0x29, 255))
        box = (x0 + 3.1, y0 + 1.9, x0 + 7.9, y0 + 8.1)
        fill, mask = digit_layer(size, str(i + 1), box,
                                 [(0.0, (0xFF, 0xF6, 0xC5)), (0.5, (0xFF, 0xEE, 0x8B)), (1.0, (0xFF, 0xE6, 0x5A))], 10)
        out = Image.new("RGBA", size, (0, 0, 0, 0))
        out.paste(fill, (0, 0), mask)
        img.alpha_composite(out)
    return img.resize((w * S, h * S), Image.LANCZOS)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    source = sys.argv[1]
    cells = [line.split() for line in open(source + ".cells")]
    for i, box in enumerate(LAPS):
        write_txt(f"vuelta_{i + 1}", cells, box, source)
        lap_badge(i + 1).save(f"textures/2d/vuelta_{i + 1}.png")
    write_txt("top3", cells, TOP3, source)
    top3_badges().save("textures/2d/top3.png")
    print("vuelta_1..5, top3")
