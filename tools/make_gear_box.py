"""Gear box pictures of the HUD (2D replacements, textures/2d/caja_<at|mt>_N):
Sergi's painted box with an empty number area (textures/art/caja_<at|mt>.png)
plus the gear's number drawn with a vector font, like the original's white
digit with a grey shade below and to the right.

  python tools/make_gear_box.py at 1 2 3 4
Needs Pillow.
"""
import sys

from PIL import Image, ImageDraw, ImageFont

kind, gears = sys.argv[1], sys.argv[2:]
box = Image.open(f"textures/art/caja_{kind}.png").convert("RGBA")
S = box.width
font = ImageFont.truetype("third_party/fonts/ArchivoBlack-Regular.ttf", int(S * 0.62))
for g in gears:
    out = box.copy()
    layer = Image.new("RGBA", box.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    # the number centred in the black area (x 0.50, y 0.715 of the box, as
    # the original's), 0.45 of the box tall
    bb = d.textbbox((0, 0), g, font=font, anchor="lt")
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    scale = S * 0.45 / h
    f = ImageFont.truetype("third_party/fonts/ArchivoBlack-Regular.ttf", int(S * 0.62 * scale))
    bb = d.textbbox((0, 0), g, font=f, anchor="lt")
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    x, y = S * 0.505 - w / 2 - bb[0], S * 0.715 - h / 2 - bb[1]
    shade = int(S * 0.025)
    d.text((x + shade, y + shade), g, font=f, fill=(110, 110, 110, 255), anchor="lt")
    d.text((x, y), g, font=f, fill=(255, 255, 255, 255), anchor="lt")
    out.alpha_composite(layer)
    out.save(f"textures/2d/caja_{kind}_{g}.png")
    print(f"textures/2d/caja_{kind}_{g}.png")
