"""Effect sprites: the attack slash and the dino dash pose.

slash() returns the "slash" sheet in art px (build_sprites.py stretches it
2x wide like every sheet): a crescent swept in front of the attacker, 3
frames each (sweeping in, full, trailing off), white with a grey edge so
the renderer can tint it per team (platform/common/scene.c draw_slash).
  row 0  side attack, facing right: 3 frames of SIDE_W x SIDE_H
  row 1  up attack: 3 frames of UP_W x UP_H (the down attack flips it)

dash_frames() makes the dash animation row of a Dino sheet from a run
frame: leaning forward, with speed lines behind.
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


if __name__ == "__main__":
    import sys
    s = slash()
    s.resize((s.width * 8, s.height * 8), Image.NEAREST).save(sys.argv[1] if len(sys.argv) > 1
                                                             else "slash.png")
