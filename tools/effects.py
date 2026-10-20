"""Effect sprites: the attack slash and the dino dash pose.

slash() returns the "slash" sheet in art px (build_sprites.py stretches it
2x wide like every sheet): a crescent swept in front of the attacker, 3
frames each (sweeping in, full, trailing off), white with a grey edge so
the renderer can tint it per team (platform/common/scene.c draw_slash).
  row 0  side attack, facing right: 3 frames of SIDE_W x SIDE_H
  row 1  up attack: 3 frames of UP_W x UP_H (the down attack flips it)

dash_frames() makes the dash animation row of a Dino sheet from a run
frame: leaning forward, with speed lines behind.

belt() returns the "belt" sheet (conveyor belts, core/level.h
SURFACE_BELT_*), BELT_CELL art px per cell, drawn over a belt cell's
terrain tile:
  row 0  belt moving right: 2 cells of rail, dark band with yellow
         chevrons every BELT_CELL px (the renderer scrolls along it)
  row 1  the same mirrored: moving left
  row 2  roller ends: left end, right end
"""
import math

from PIL import Image

SIDE_W, SIDE_H = 16, 24
UP_W, UP_H = 24, 16
CORE = (255, 255, 255, 255)
EDGE = (176, 184, 200, 255)

# Per frame: swept angle range (degrees, 0 = straight ahead, negative = up)
# and thickness at the middle of the arc.
SWEEP = [(-75, 5, 3), (-75, 75, 5), (25, 75, 2)]


def crescent(w, h, cx, cy, radius, a0, a1, thick, axis):
    """Arc pixels around (cx, cy): angle measured from the +x axis (side
    slash) or the -y axis (up slash, axis "up"), thickest mid-sweep."""
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            r = math.hypot(dx, dy)
            a = math.degrees(math.atan2(dy, dx) if axis == "side" else math.atan2(dx, -dy))
            if not a0 <= a <= a1:
                continue
            t = (a - (-75)) / 150                   # position along the full arc, 0..1
            band = thick * math.sin(math.pi * min(max(t, 0.05), 0.95))
            if radius - band - 1 <= r <= radius:
                px[x, y] = CORE if r >= radius - band else EDGE
            elif radius < r <= radius + 1 and band >= 1:
                px[x, y] = EDGE
    return im


def slash():
    sheet = Image.new("RGBA", (3 * UP_W, SIDE_H + UP_H))
    for i, (a0, a1, thick) in enumerate(SWEEP):
        sheet.paste(crescent(SIDE_W, SIDE_H, 1, SIDE_H / 2, 11, a0, a1, thick, "side"),
                    (i * SIDE_W, 0))
        sheet.paste(crescent(UP_W, UP_H, UP_W / 2, UP_H - 1, 11, a0, a1, thick, "up"),
                    (i * UP_W, SIDE_H))
    return sheet


SPEED = (236, 244, 255, 255)


def dash_frames(run, foot=21):
    """3 frames from a right-facing run frame: rows shifted forward (more
    toward the head) and three speed lines behind the body, their lengths
    cycling per frame."""
    src = run.load()
    w, h = run.size
    frames = []
    for f in range(3):
        im = Image.new("RGBA", (w, h))
        px = im.load()
        for y in range(h):
            shift = max(foot - y, 0) // 7      # 0..3 px: lean in steps of 7 rows
            for x in range(w):
                if src[x, y][3] and 0 <= x + shift < w:
                    px[x + shift, y] = src[x, y]
        for i, y in enumerate((9, 13, 17)):
            left = next((x for x in range(w) if px[x, y][3]), None)
            if left is None:
                continue
            length = (4, 7, 5)[(i + f) % 3]
            for x in range(max(left - 2 - length, 0), max(left - 2, 0)):
                px[x, y] = SPEED
        frames.append(im)
    return frames


BELT_CELL = 8
RAIL = (184, 192, 200, 255)
BAND = (48, 52, 58, 255)
CHEVRON = (232, 184, 48, 255)
UNDER = (30, 32, 36, 255)
HUB = (128, 136, 144, 255)


def belt():
    c = BELT_CELL
    im = Image.new("RGBA", (2 * c, 3 * c))
    px = im.load()
    for x in range(2 * c):
        px[x, 0] = RAIL
        for y in range(1, 6):
            px[x, y] = BAND
        px[x, 6] = UNDER
        px[x, 7] = HUB if x % 4 == 1 else UNDER     # rollers under the band
        for y in range(1, 6):                       # ">" two px thick, tip on row 3
            if x % c in (2 - abs(y - 3) + 2, 2 - abs(y - 3) + 3):
                px[x, y] = CHEVRON
    for x in range(2 * c):
        for y in range(c):
            px[x, c + y] = px[2 * c - 1 - x, y]
    # Ends: a drum with a hub, the band wrapping round it.
    for side in range(2):
        for y in range(c):
            for x in range(c):
                dx = (c - 1 - x) if side else x     # 0 at the outer edge
                if (dx, y) in ((0, 0), (0, 7), (0, 1), (0, 6), (1, 0), (1, 7)):
                    continue                        # rounded corners
                col = RAIL if y == 0 else UNDER if y >= 6 else BAND
                if dx == 0 or (dx == 1 and y in (1, 6)):
                    col = RAIL
                if 2 <= dx <= 4 and 2 <= y <= 4:
                    col = HUB if (dx, y) != (3, 3) else UNDER
                px[side * c + x, 2 * c + y] = col
    return im


if __name__ == "__main__":
    import sys
    s = slash()
    s.resize((s.width * 8, s.height * 8), Image.NEAREST).save(sys.argv[1] if len(sys.argv) > 1
                                                             else "slash.png")
