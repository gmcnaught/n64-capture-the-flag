#!/usr/bin/env python3
"""Build every sprite sheet for both platforms.

usage: build_sprites.py <assets_dir>

Writes <assets_dir>/n64/<name>.png (mksprite input; the sky sheets paletted,
for CI8) and
<assets_dir>/host/<name>.rgba (raw RGBA: "RGBA", u16 w, u16 h big-endian,
then w*h*4 bytes) for each sheet in platform/common/scene.c's
scene_sheet_name: the tiles once per tileset, the sky and scenery ones
once per backdrop. Sources are the square-pixel art packs in new-assets/;
every sheet is stretched 2x horizontally (nearest), because the game runs
at 640x240 where a 4:3 display shows pixels half as wide (core/level.h).
Layouts below are in art px (before the stretch):

  tiles_<tileset>  terrain (tools/terrain.py), already at sim resolution:
            16x8 sim px cells, 16 columns; corners, edges and fill built
            from assets/src/tilesets/arena.png (Tiny Adventure Forest) or a
            Rocky Roads tileset; also writes platform/common/tilesets.h
  p1        Dino Characters "mort" (red), 24x24 frames, one row per animation
            in core/player.h order (DINO_ANIMS): idle, run, jump, climb,
            death, hurt, attack, then dash (tools/effects.py: a run frame
            leaning forward with speed lines, 3 frames)
  p2        Dino Characters "doux" (blue), same layout
  p3        p1 lighter (light red: saturated mid tones paler and brighter)
  p4        p2 darker (dark blue)
  jumppad   Rocky Roads bounce pad halved (nearest): 4 frames of 8x16
  flag      Rocky Roads flag halved (nearest): 9 frames of 16x16; row 0 red
            (team 1), row 1 with the cloth pale blue (team 2), both with a 1 px
            dark border around the cloth; row 2 (first cell) a dotted
            outline of frame 0 for empty goal stands
  sky_<backdrop>, scenery_<backdrop>  the backdrops (tools/scenery.py),
            already at sim resolution (640 wide, not stretched); also writes
            their names and sky heights to platform/common/backdrops.h
  hud       Rocky Roads hearts halved (nearest): full, 3/4, 1/2, empty, 8x8
  fruit     Rocky Roads apple halved (nearest) and recolored blue: the water
            fruit, 8x8
  font      display font (tools/font.py), medium, large and small glyph
            grids; also writes their layout to platform/common/font_glyphs.h
  slash     attack slash (tools/effects.py): 3 side frames of 16x24, 3 up
            frames of 24x16
  belt      conveyor belts (tools/effects.py): 8x8 cells; rows moving right,
            moving left, roller ends
  controls  N64 controller picture for the CONTROLS screen (tools/controls.py),
            already at sim resolution (232x108, not stretched)
  platform  Rocky Roads cloud platforms halved (nearest), for one-way moving
            platforms 2, 3 and 4 cells wide: one 34x10 slot each, the cloud
            centered, its top on the slot's top row
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
import terrain

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


def tone(im, sat, val):
    """Scale saturation and value of the saturated mid tones (the body
    colors; outlines and the belly stay)."""
    out = im.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if a and s > 0.35 and v > 0.4:
                r, g, b = colorsys.hsv_to_rgb(h, min(1.0, s * sat), min(1.0, v * val))
                px[x, y] = (round(r * 255), round(g * 255), round(b * 255), a)
    return out


def is_cloth(px):
    """A flag cloth pixel: saturated red (the pole is dark purple)."""
    r, g, b, a = px
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return a and s > 0.35 and v > 0.4 and (h > 0.9 or h < 0.08)


def is_blue_cloth(px):
    r, g, b, a = px
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return a and s > 0.15 and v > 0.6 and 0.5 < h < 0.7


def blue_cloth(im, hue=0.6, sat=0.45, val=1.25):
    """The red flag with its cloth pale blue (the pole kept): lighter than
    the blue skies, it reads on them with the dark border."""
    out = im.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            if is_cloth(px[x, y]):
                r, g, b, a = px[x, y]
                _, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                r, g, b = colorsys.hsv_to_rgb(hue, min(1.0, s * sat), min(1.0, v * val))
                px[x, y] = (round(r * 255), round(g * 255), round(b * 255), a)
    return out


def cloth_border(im, color=(16, 20, 48, 255), cloth=None):
    """A 1 px dark border around each frame's cloth (transparent pixels next
    to a cloth pixel), so the flag reads on any sky."""
    cloth = cloth or is_cloth
    out = im.copy()
    src, px = im.load(), out.load()
    for y in range(im.height):
        for x in range(im.width):
            if src[x, y][3]:
                continue
            fx, fy = x // 16 * 16, y // 16 * 16       # 16x16 frames: no border across one
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if fx <= nx < fx + 16 and fy <= ny < fy + 16 and cloth(src[nx, ny]):
                    px[x, y] = color
                    break
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
    p1, p2 = dino("mort"), dino("doux")
    yield "p1", p1
    yield "p2", p2
    yield "p3", tone(p1, 0.5, 1.3)
    yield "p4", tone(p2, 1.1, 0.6)
    pad = load(os.path.join(ROCKY, "Objects", "bounce pad.png"))
    yield "jumppad", pad.resize((pad.width // 2, pad.height // 2), Image.NEAREST)
    flag = load(os.path.join(ROCKY, "Objects", "flag.png"))
    flag = flag.resize((flag.width // 2, flag.height // 2), Image.NEAREST)
    yield "flag", stack([cloth_border(flag), cloth_border(blue_cloth(flag), cloth=is_blue_cloth),
                         dotted_outline(flag.crop((0, 0, 16, 16)))])
    apple = load(os.path.join(ROCKY, "Objects", "apple.png"))
    yield "fruit", recolor(apple.resize((8, 8), Image.NEAREST), 0.55)
    hearts = load(os.path.join(ROCKY, "UI", "hearts.png"))
    yield "hud", hearts.resize((hearts.width // 2, hearts.height // 2), Image.NEAREST)
    yield "font", font.sheet()
    yield "slash", effects.slash()
    yield "belt", effects.belt()
    yield "platform", clouds()


# Cloud bounding boxes in Objects/cloud_platforms.png (source px), for one-way
# platforms 2, 3 and 4 cells (16, 24, 32 art px) wide.
CLOUDS = [(111, 63, 34, 13), (111, 111, 50, 14), (15, 158, 66, 18)]
CLOUD_SLOT = (34, 10)


def clouds():
    src = load(os.path.join(ROCKY, "Objects", "cloud_platforms.png"))
    out = Image.new("RGBA", (CLOUD_SLOT[0] * len(CLOUDS), CLOUD_SLOT[1]))
    for i, (x, y, w, h) in enumerate(CLOUDS):
        im = src.crop((x, y, x + w, y + h))
        im = im.resize((w // 2, h // 2), Image.NEAREST)
        out.paste(im, (i * CLOUD_SLOT[0] + (CLOUD_SLOT[0] - im.width) // 2, 0))
    return out


def paletted(im):
    """Opaque im as a paletted image of its colors cut to 5 bits a channel
    (what RGBA16 keeps), for a CI8 sheet: mksprite keeps a PNG's palette."""
    rgb = im.convert("RGB").point(lambda c: c & 0xF8)
    colors = sorted({c for _, c in rgb.getcolors(1 << 24)})
    if len(colors) > 256:
        raise SystemExit(f"{len(colors)} colors: more than CI8 holds")
    index = {c: i for i, c in enumerate(colors)}
    out = Image.frombytes("P", im.size, bytes(index[c] for c in rgb.getdata()))
    out.putpalette([v for c in colors for v in c])
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(os.path.join(out, "n64"), exist_ok=True)
    os.makedirs(os.path.join(out, "host"), exist_ok=True)
    native = scenery.sheets() + [("controls", controls.pad())]     # already at sim resolution
    native += [("tiles_" + n, terrain.sheet(n)) for n in terrain.TILESETS]
    for name, im in [(n, im.resize((im.width * 2, im.height), Image.NEAREST))
                     for n, im in sheets()] + native:
        if im.width > 1024:
            raise SystemExit(f"{name}: {im.width} px wide, RDP textures are at most 1024")
        (paletted(im) if name.startswith("sky_") else im).save(os.path.join(out, "n64", name + ".png"))
        with open(os.path.join(out, "host", name + ".rgba"), "wb") as f:
            f.write(b"RGBA" + struct.pack(">HH", im.width, im.height) + im.tobytes())
        print(f"{name}: {im.width}x{im.height}")
    for name, text in [("font_glyphs.h", font.header()), ("backdrops.h", scenery.header()),
                       ("tilesets.h", terrain.header())]:
        path = os.path.join(ROOT, "platform", "common", name)
        if not os.path.exists(path) or open(path).read() != text:
            with open(path, "w") as f:
                f.write(text)
            print(f"wrote {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    main()
