"""Backdrop: the drifting sky and the static scenery behind the arena.

sheets() returns two sheets already at sim resolution (640 px wide, so
build_sprites.py does not stretch them):
  sky      SKY_W x SKY_H, opaque; drawn every frame, scrolled left,
           alternately mirrored so every edge joins its neighbor
  scenery  640 x 240 with a transparent top through which the sky shows;
           drawn once per level into the background

With Bongseng's "Parallax (Forest, desert, sky, moon)" in
new-assets/more-parallax (free to use in games, but its files may not be
redistributed, so it is not in the repository), the forest set is used:
its art is drawn in 2 px blocks at 3800x2400, so scaling it to 640 columns
keeps detail in every one of them. Without it, the build falls back to
The Pixel Nook mountains and the Rocky Roads clouds and trees (art px,
stretched 2x like every other sheet).
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOREST = os.path.join(ROOT, "new-assets", "more-parallax", "parallax", "forest")
MOUNTAINS = os.path.join(ROOT, "new-assets", "parallax-backgrounds", "Free", "Mountain",
                         "2.Mountains.png")
ROCKY_BG = os.path.join(ROOT, "new-assets", "RockyRoads", "Rocky Roads", "Backgrounds")

VIEW_W, VIEW_H = 640, 240
SKY_W, SKY_H = 640, 144         # platform/common/scene.c draws this much of it
# Forest layers back to front (the sky goes into the sky sheet; the faint
# moon is left out: the mirrored sky would show it twice).
FOREST_SCENERY = ["forest_mountain.png", "forest_back.png", "forest_mid.png",
                  "forest_short.png", "forest_long.png"]


def load(path):
    return Image.open(path).convert("RGBA")


def forest_layer(name, w, rows):
    """A forest layer on the pack's 3800x2400 canvas (the sky is drawn at
    half that, forest_long is the bottom half), scaled to w x rows."""
    im = load(os.path.join(FOREST, name))
    if im.size == (1900, 1200):
        im = im.resize((3800, 2400), Image.NEAREST)
    elif im.size == (3800, 1200):
        canvas = Image.new("RGBA", (3800, 2400))
        canvas.paste(im, (0, 1200))
        im = canvas
    return im.resize((w, rows), Image.LANCZOS)


def forest():
    # The pack's square pixels across the 320 display units of the 4:3
    # screen: 202 rows, bottom-aligned; the sky's top color fills above.
    rows = round(2400 * 320 / 3800)
    top = VIEW_H - rows
    sky_layer = forest_layer("forest_sky.png", SKY_W, rows)
    sky = Image.new("RGBA", (SKY_W, SKY_H), sky_layer.getpixel((0, 0)))
    sky.alpha_composite(sky_layer.crop((0, 0, SKY_W, SKY_H - top)), (0, top))
    scenery = Image.new("RGBA", (VIEW_W, VIEW_H))
    layer = Image.new("RGBA", (VIEW_W, rows))
    for name in FOREST_SCENERY:
        layer.alpha_composite(forest_layer(name, VIEW_W, rows))
    scenery.alpha_composite(layer, (0, top))
    return sky, scenery


def fallback():
    """The shareable backdrop, in art px (320 wide), stretched 2x."""
    sky = Image.new("RGBA", (SKY_W // 2, SKY_H), (166, 218, 242, 255))
    for i, c in enumerate([(95, 182, 232), (116, 194, 236), (140, 206, 239)]):
        sky.paste(c + (255,), (0, i * 16, SKY_W // 2, i * 16 + 16))
    clouds = load(os.path.join(ROCKY_BG, "clouds.png"))
    clouds = clouds.resize((SKY_W // 2, clouds.height), Image.NEAREST)  # 256 -> 320 art px
    sky.alpha_composite(clouds, (0, -10))
    scenery = Image.new("RGBA", (VIEW_W // 2, VIEW_H))
    mountains = load(MOUNTAINS)
    scenery.alpha_composite(mountains.crop((0, 23, 320, 180)), (0, 46))
    trees = load(os.path.join(ROCKY_BG, "trees.png"))
    for x in (0, trees.width):
        scenery.alpha_composite(trees, (x, 112)) if x + trees.width <= 320 else \
            scenery.alpha_composite(trees.crop((0, 0, 320 - x, trees.height)), (x, 112))
    stretch = lambda im: im.resize((im.width * 2, im.height), Image.NEAREST)
    return stretch(sky), stretch(scenery)


def sheets():
    sky, scenery = forest() if os.path.isdir(FOREST) else fallback()
    return [("sky", sky), ("scenery", scenery)]
