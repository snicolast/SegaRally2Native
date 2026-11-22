"""The black STACK panel beside the Celica's rev counter (a 64x32 texture of
the dial's 3D model, ARGB1555, texture word 005b00c0) redrawn 16 times larger
for the texture pack: textures/afc75b95e2858cc0.png (the name is the hash of
the original texture, src/video/texture_pack.h). Three lines of STACK, white,
red, white, upside down as the game stores them.

  python tools/make_stack_panel.py
Needs Pillow.
"""
from PIL import Image, ImageDraw, ImageFont

S, SS = 16, 4
W, H = 64 * S * SS, 32 * S * SS
img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
d = ImageDraw.Draw(img)
font = ImageFont.truetype("third_party/fonts/Arimo[wght].ttf", 100)
try:
    font.set_variation_by_axes([700])
except Exception:
    pass
# the letters of each line in the original: x 18..45, 5 rows high, centred on rows 8.5, 15.5, 22.5 (after the flip)
x0, x1, cap = 18.2 * S * SS, 45.8 * S * SS, 5.2 * S * SS
for centre, colour in ((9.0, (255, 255, 255, 255)), (16.0, (230, 16, 16, 255)), (23.0, (255, 255, 255, 255))):
    line = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(line)
    box = ld.textbbox((0, 0), "STACK", font=font, anchor="ls")
    glyphs = Image.new("RGBA", (box[2] - box[0] + 8, box[3] - box[1] + 8), (0, 0, 0, 0))
    ImageDraw.Draw(glyphs).text((4 - box[0], 4 - box[1]), "STACK", font=font, fill=colour, anchor="ls")
    glyphs = glyphs.crop(glyphs.getbbox()).resize((int(x1 - x0), int(cap)), Image.LANCZOS)
    img.alpha_composite(glyphs, (int(x0), int(centre * S * SS - cap / 2)))
img = img.transpose(Image.FLIP_TOP_BOTTOM).resize((64 * S, 32 * S), Image.LANCZOS)
img.save("textures/afc75b95e2858cc0.png")
print("textures/afc75b95e2858cc0.png", img.size)
