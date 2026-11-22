"""The bar graph rev counters (Impreza, Delta) as 2D replacements with a lit
state (src/video/pack2d.h): textures/2d/barras_<car>_<part>.txt from a
capture at idle (all bars off) and <..>_lit.txt from one at the rev limiter
(all bars on). Each counter is cut in two boxes (above and left of the gear
box, which has its own replacement: boxes must not overlap). The captures
come from snapshots replayed with LW_2D_CAPTURE (out/2d/barras_coche_N_*).

  python tools/make_bar_tachs.py          the .txt files and the templates
  python tools/make_bar_tachs.py split    cut the pictures
      (textures/art/barras_<car>.png and barras_<car>_lit.png, each the
      whole counter as the template, from tools/make_bar_art.py) into the boxes
Needs Pillow.
"""
import sys

from PIL import Image

CARS = {   # car: (capture number, the counter's whole box in cells, its parts (x, y, w, h) in cells)
    "impreza": (3, (2, 33, 14, 13), [("a", (2, 33, 14, 9)), ("b", (2, 42, 5, 4))]),
    "delta": (7, (2, 36, 16, 9), [("a", (2, 36, 16, 6)), ("b", (2, 42, 5, 3))]),
}


def cells(path):
    return [line.split() for line in open(path + ".cells")]


def pixels(path):
    img = Image.open(path + ".png").convert("RGBA")
    return img.load()


def write(name, grid, box, source, other):
    """other: the cells of the other state; where a cell differs, its pixels in
    this state go in a "px" line (src/video/pack2d.h), so a cell caught
    between the two states shows the closer one"""
    x, y, w, h = box
    px = pixels(source)
    with open(f"textures/2d/{name}.txt", "w") as f:
        f.write(f"# bar graph rev counter, from {source}\nrect {x} {y} {w} {h}\n")
        for r in range(y, y + h):
            for c in range(x, x + w):
                f.write(grid[r][c] + "\n")
        for r in range(y, y + h):
            for c in range(x, x + w):
                if grid[r][c] == other[r][c]:
                    continue
                values = []
                for py in range(8):
                    for pxx in range(8):
                        cr, cg, cb, ca = px[c * 8 + pxx, r * 8 + py]
                        values.append(f"{ca << 24 | cr << 16 | cg << 8 | cb:08x}")
                f.write(f"px {(r - y) * w + c - x} " + " ".join(values) + "\n")


def make():
    for car, (n, whole, parts) in CARS.items():
        off, on = f"out/2d/barras_coche_{n}_ralenti", f"out/2d/barras_coche_{n}_bajo"
        for part, box in parts:
            write(f"barras_{car}_{part}", cells(off), box, off, cells(on))
            write(f"barras_{car}_{part}_lit", cells(on), box, on, cells(off))
        x, y, w, h = whole
        for state, src in (("apagado", off), ("encendido", on)):
            img = Image.open(src + ".png").convert("RGBA").crop((x * 8, y * 8, (x + w) * 8, (y + h) * 8))
            img.resize((img.width * 8, img.height * 8), Image.NEAREST).save(f"out/2d/barras_{car}_{state}_8x.png")
        print(car, "ok")


def split():
    for car, (n, whole, parts) in CARS.items():
        for suffix in ("", "_lit"):
            art = Image.open(f"textures/art/barras_{car}{suffix}.png").convert("RGBA")
            X, Y, W, H = whole
            kx, ky = art.width / (W * 8), art.height / (H * 8)
            for part, (x, y, w, h) in parts:
                box = (round((x - X) * 8 * kx), round((y - Y) * 8 * ky), round((x - X + w) * 8 * kx),
                       round((y - Y + h) * 8 * ky))
                art.crop(box).save(f"textures/2d/barras_{car}_{part}{suffix}.png")
        print(car, "split")


if __name__ == "__main__":
    split() if sys.argv[1:] == ["split"] else make()
