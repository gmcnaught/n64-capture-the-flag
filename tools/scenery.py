"""Backdrops: the drifting sky and the static scenery behind the arena.

One pair of sheets per backdrop (BACKDROPS; the order is the level
`backdrop` id, core/level.h BACKDROP_*), already at sim resolution (640 px
wide, so build_sprites.py does not stretch them):
  sky_<name>      640 x sky_h, opaque, tiling horizontally; drawn every
                  frame, scrolled left and repeated
  scenery_<name>  640 x 240 with a transparent top through which the sky
                  shows; drawn once per level into the background. From
                  row sky_h down it is over a still copy of the sky, so it
                  is opaque there (the layers' soft edges included)
header() is platform/common/backdrops.h: names and sky heights for
platform/common/scene.c.

The art is Bongseng's "Parallax (Forest, desert, sky, moon)" in
new-assets/more-parallax (free to use in games, but its files may not be
redistributed, so it is not in the repository). Its forest set is drawn in
2 px blocks at 3800x2400; the desert, moon and sky sets on a 1900x1000
canvas, with 3800-wide layers holding two screens of a horizontal loop
(the first is used). Each set is scaled to 640 columns (all of it across
the screen) and bottom-aligned; the sky's top color fills above. The
pack's sky layers tile horizontally, and are scaled with wrapped edges so
the 640 px sheets still do. Without the pack, every backdrop falls back to
The Pixel Nook mountains and the Rocky Roads clouds and trees (art px,
stretched 2x like every other sheet), its sky made tileable by mirroring
its left half.
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.path.join(ROOT, "new-assets", "more-parallax", "parallax")
MOUNTAINS = os.path.join(ROOT, "new-assets", "parallax-backgrounds", "Free", "Mountain",
                         "2.Mountains.png")
ROCKY_BG = os.path.join(ROOT, "new-assets", "RockyRoads", "Rocky Roads", "Backgrounds")

VIEW_W, VIEW_H = 640, 240
SKY_W = 640

# name, pack folder, sky layers, scenery layers (back to front), sky_h:
# the rows the drifting sky covers, down to where the scenery is (nearly)
# opaque. The moons are left out: the mirrored sky would show them twice.
BACKDROPS = [
    ("forest", "forest", ["forest_sky"],
     ["forest_mountain", "forest_back", "forest_mid", "forest_short", "forest_long"], 144),
    ("desert", "desert", ["desert_sky", "desert_cloud"],
     ["desert_mountain", "desert_dunemid", "desert_dunefrontt"], 184),
    ("moon", "moon", ["moon_sky"],
     ["moon_earth", "moon_back", "moon_mid", "moon_front", "moon_floor"], 168),
    ("sky", "skies", ["Sky_sky", "sky_clouds", "Sky_cloud_single"],
     ["Sky_back_mountain", "sky_front_mountain", "sky_cloud_floor", "sky_cloud_floor_2",
      "Sky_front_cloud"], 152),
]


def load(path):
    return Image.open(path).convert("RGBA")


def resize(im, w, h, wrap):
    """Scale to w x h; wrap: as a horizontal loop (padded with its other
    end by 1/20 of the width, so the edges are resampled as neighbors)."""
    if not wrap:
        return im.resize((w, h), Image.LANCZOS)
    pad = im.width // 20
    padded = Image.new("RGBA", (im.width + 2 * pad, im.height))
    padded.paste(im.crop((im.width - pad, 0, im.width, im.height)), (0, 0))
    padded.paste(im, (pad, 0))
    padded.paste(im.crop((0, 0, pad, im.height)), (pad + im.width, 0))
    pw = w // 20
    return padded.resize((w + 2 * pw, h), Image.LANCZOS).crop((pw, 0, pw + w, h))


def forest_layer(name, rows, wrap):
    """A forest layer on the pack's 3800x2400 canvas (the sky is drawn at
    half that, forest_long is the bottom half), scaled to 640 x rows."""
    im = load(os.path.join(PACK, "forest", name + ".png"))
    if im.size == (1900, 1200):
        im = im.resize((3800, 2400), Image.NEAREST)
    elif im.size == (3800, 1200):
        canvas = Image.new("RGBA", (3800, 2400))
        canvas.paste(im, (0, 1200))
        im = canvas
    return resize(im, VIEW_W, rows, wrap)


def screen_layer(folder, name, rows, wrap):
    """A layer on the 1900x1000 canvas (first screen of a 3800 loop),
    scaled to 640 x rows."""
    im = load(os.path.join(PACK, folder, name + ".png"))
    if im.width == 3800:
        im = im.crop((0, 0, 1900, im.height))
    return resize(im, VIEW_W, rows, wrap)


def pack_backdrop(folder, sky_names, scenery_names):
    # The pack's square pixels across the 320 display units of the 4:3
    # screen, bottom-aligned.
    if folder == "forest":
        rows = round(2400 * 320 / 3800)
        layer = lambda n, wrap=False: forest_layer(n, rows, wrap)
    else:
        rows = round(1000 * 320 / 1900)
        layer = lambda n, wrap=False: screen_layer(folder, n, rows, wrap)
    top = VIEW_H - rows
    sky_layer = Image.new("RGBA", (SKY_W, rows))
    for name in sky_names:
        sky_layer.alpha_composite(layer(name, wrap=True))
    sky = Image.new("RGBA", (SKY_W, VIEW_H), sky_layer.getpixel((0, 0)))
    sky.alpha_composite(sky_layer, (0, top))
    scenery = Image.new("RGBA", (VIEW_W, VIEW_H))
    for name in scenery_names:
        scenery.alpha_composite(layer(name), (0, top))
    return sky, scenery


def fallback():
    """The shareable backdrop, in art px (320 wide), stretched 2x."""
    sky = Image.new("RGBA", (SKY_W // 2, VIEW_H), (166, 218, 242, 255))
    for i, c in enumerate([(95, 182, 232), (116, 194, 236), (140, 206, 239)]):
        sky.paste(c + (255,), (0, i * 16, SKY_W // 2, i * 16 + 16))
    clouds = load(os.path.join(ROCKY_BG, "clouds.png"))
    clouds = clouds.resize((SKY_W // 2, clouds.height), Image.NEAREST)  # 256 -> 320 art px
    sky.alpha_composite(clouds, (0, -10))
    sky.paste(sky.crop((0, 0, SKY_W // 4, VIEW_H)).transpose(Image.FLIP_LEFT_RIGHT), (SKY_W // 4, 0))
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
    """(sheet name, image) pairs. pack_backdrop and fallback return a
    full-screen still sky; the sky sheet is its top sky_h rows, and the
    rest goes under the scenery."""
    out = []
    for name, folder, sky_names, scenery_names, sky_h in BACKDROPS:
        if os.path.isdir(os.path.join(PACK, folder)):
            sky, scenery = pack_backdrop(folder, sky_names, scenery_names)
        else:
            sky, scenery = fallback()
        base = Image.new("RGBA", (VIEW_W, VIEW_H))
        base.paste(sky.crop((0, sky_h, VIEW_W, VIEW_H)), (0, sky_h))
        base.alpha_composite(scenery)
        out += [("sky_" + name, sky.crop((0, 0, SKY_W, sky_h))), ("scenery_" + name, base)]
    return out


def header():
    names = ", ".join(f'"{b[0]}"' for b in BACKDROPS)
    heights = ", ".join(str(b[4]) for b in BACKDROPS)
    return f"""/* Generated by tools/build_sprites.py from tools/scenery.py: backdrops, in
 * level `backdrop` id order (core/level.h BACKDROP_*). Sheets "sky_<name>"
 * (640 x sky_h, drifting) and "scenery_<name>" (640 x 240). */
#ifndef BACKDROPS_H
#define BACKDROPS_H

#include "level.h"

/* Fails to compile if core/level.h BACKDROP_COUNT and tools/scenery.py
 * BACKDROPS disagree. */
typedef char backdrop_count_matches[BACKDROP_COUNT == {len(BACKDROPS)} ? 1 : -1];

static const char *const backdrop_names[BACKDROP_COUNT] = {{{names}}};
static const unsigned char backdrop_sky_h[BACKDROP_COUNT] = {{{heights}}};

#endif
"""
