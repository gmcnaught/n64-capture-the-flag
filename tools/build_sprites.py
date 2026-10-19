#!/usr/bin/env python3
"""Build every sprite sheet for both platforms.

usage: build_sprites.py <assets_dir>

Writes <assets_dir>/n64/<name>.png (mksprite input) and
<assets_dir>/host/<name>.rgba (raw RGBA: "RGBA", u16 w, u16 h big-endian,
then w*h*4 bytes) for each sheet in platform/common/scene.c's
scene_sprite_names. Sources are the square-pixel art packs in new-assets/;
every sheet is stretched 2x horizontally (nearest), because the game runs
at 640x240 where a 4:3 display shows pixels half as wide (core/level.h).
Layouts below are in art px (before the stretch):

  tiles     assets/src/tilesets/arena.png (tools/make_tileset.py), 8x8 cells,
            17 columns (Tiny Adventure Forest ground)
  p1        Dino Characters "mort" (red), 24x24 frames, one row per animation
            in core/player.h order (DINO_ANIMS): idle, run, jump, climb,
            death, hurt, attack, then dash (tools/effects.py: a run frame
            leaning forward with speed lines, 3 frames)
  p2        Dino Characters "doux" (blue), same layout
  jumppad   Rocky Roads bounce pad halved (nearest): 4 frames of 8x16
  flag      Rocky Roads flag halved (nearest): 9 frames of 16x16; row 0 red
            (team 1), row 1 recolored blue (team 2), row 2 (first cell) a
            dotted outline of frame 0 for empty goal stands
  sky, scenery  the backdrop (tools/scenery.py), already at sim resolution
            (640 wide, not stretched)
  hud       Rocky Roads hearts halved (nearest): full, 3/4, 1/2, empty, 8x8
  fruit     Rocky Roads apple halved (nearest) and recolored blue: the water
            fruit, 8x8
  font      display font (tools/font.py), medium, large and small glyph
            grids; also writes their layout to platform/common/font_glyphs.h
  slash     attack slash (tools/effects.py): 3 side frames of 16x24, 3 up
            frames of 24x16
  controls  N64 controller picture for the CONTROLS screen (tools/controls.py),
            128x84
"""
import colorsys
import os
import struct
import sys

from PIL import Image

import controls
import effects
import font
import scenery

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROCKY = os.path.join(ROOT, "new-assets", "RockyRoads", "Rocky Roads")
DINOS = os.path.join(ROOT, "new-assets", "sheets")
FRAME = 24
# Source frame per animation row (core/player.h ANIM_*) of the 24-frame
# DinoSprites strip: 0-3 idle, 4-9 move, 10-12 kick, 13-16 hurt (15 is a
# white flash), 17-23 crouch. The pack has no jump or climb frames: jump
# slots 3/4/5 (rising/apex/falling, core/player.c animate) use kick and
# move frames, slot 7 (landing) the crouch; climb repeats idle.
DINO_ANIMS = [
    [0, 1, 2, 3],                       # idle
    [4, 5, 6, 7, 8, 9],                 # run
    [0, 0, 0, 12, 10, 8, 0, 17],        # jump
    [0, 1, 2, 3],                       # climb
    [13, 14, 15, 16, 15, 14, 15, 15],   # death
    [13, 14, 15, 16],                   # hurt
    [10, 11, 12, 12],                   # attack
]
DASH_FROM = 8                           # run frame the dash row leans (ANIM_DASH, after DINO_ANIMS)


def load(path):
    return Image.open(path).convert("RGBA")


def dino(name):
    strip = load(os.path.join(DINOS, f"DinoSprites - {name}.png"))
    cols = max(len(r) for r in DINO_ANIMS)
    sheet = Image.new("RGBA", (cols * FRAME, (len(DINO_ANIMS) + 1) * FRAME))
    for row, frames in enumerate(DINO_ANIMS):
        for col, f in enumerate(frames):
            sheet.paste(strip.crop((f * FRAME, 0, f * FRAME + FRAME, FRAME)), (col * FRAME, row * FRAME))
    run = strip.crop((DASH_FROM * FRAME, 0, DASH_FROM * FRAME + FRAME, FRAME))
    for col, im in enumerate(effects.dash_frames(run)):
        sheet.paste(im, (col * FRAME, len(DINO_ANIMS) * FRAME))
    return sheet


def recolor(im, hue_shift):
    """Rotate the hue of saturated pixels (the red cloth of the flag)."""
    out = im.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if a and s > 0.35:
                r, g, b = colorsys.hsv_to_rgb((h + hue_shift) % 1.0, s, v)
                px[x, y] = (round(r * 255), round(g * 255), round(b * 255), a)
    return out


def dotted_outline(frame, color=(236, 236, 244, 255)):
    """Every other edge pixel of a sprite's silhouette (pixels next to a
    transparent one), checkerboard order."""
    src = frame.load()
    out = Image.new("RGBA", frame.size)
    px = out.load()
    w, h = frame.size

    def solid(x, y):
        return 0 <= x < w and 0 <= y < h and src[x, y][3] >= 128

    for y in range(h):
        for x in range(w):
            edge = solid(x, y) and not all(solid(x + dx, y + dy)
                                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if edge and (x + y) % 2 == 0:
                px[x, y] = color
    return out


def stack(images):
    sheet = Image.new("RGBA", (max(i.width for i in images), sum(i.height for i in images)))
    y = 0
    for im in images:
        sheet.paste(im, (0, y))
        y += im.height
    return sheet


def sheets():
    yield "tiles", load(os.path.join(ROOT, "assets", "src", "tilesets", "arena.png"))
    yield "p1", dino("mort")
    yield "p2", dino("doux")
    pad = load(os.path.join(ROCKY, "Objects", "bounce pad.png"))
    yield "jumppad", pad.resize((pad.width // 2, pad.height // 2), Image.NEAREST)
    flag = load(os.path.join(ROCKY, "Objects", "flag.png"))
    flag = flag.resize((flag.width // 2, flag.height // 2), Image.NEAREST)
    yield "flag", stack([flag, recolor(flag, 0.6), dotted_outline(flag.crop((0, 0, 16, 16)))])
    apple = load(os.path.join(ROCKY, "Objects", "apple.png"))
    yield "fruit", recolor(apple.resize((8, 8), Image.NEAREST), 0.55)
    hearts = load(os.path.join(ROCKY, "UI", "hearts.png"))
    yield "hud", hearts.resize((hearts.width // 2, hearts.height // 2), Image.NEAREST)
    yield "font", font.sheet()
    yield "slash", effects.slash()
    yield "controls", controls.pad()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(os.path.join(out, "n64"), exist_ok=True)
    os.makedirs(os.path.join(out, "host"), exist_ok=True)
    native = scenery.sheets()           # already 640 wide
    for name, im in list(sheets()) + native:
        if (name, im) not in native:
            im = im.resize((im.width * 2, im.height), Image.NEAREST)
        if im.width > 1024:
            raise SystemExit(f"{name}: {im.width} px wide, RDP textures are at most 1024")
        im.save(os.path.join(out, "n64", name + ".png"))
        with open(os.path.join(out, "host", name + ".rgba"), "wb") as f:
            f.write(b"RGBA" + struct.pack(">HH", im.width, im.height) + im.tobytes())
        print(f"{name}: {im.width}x{im.height}")
    path = os.path.join(ROOT, "platform", "common", "font_glyphs.h")
    text = font.header()
    if not os.path.exists(path) or open(path).read() != text:
        with open(path, "w") as f:
            f.write(text)
        print(f"wrote {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    main()
