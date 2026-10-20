"""N64 controller picture for the pause menu's CONTROLS screen.

pad() returns it at sim resolution, W x H (build_sprites.py does not stretch
it): a sim px shows half as wide as tall, so the picture is twice as many px
wide as a square-pixel one. The renderer draws labels and leader lines to
the parts at PARTS (centers and radii in art px, i.e. half sim px across,
from the picture's top-left; platform/common/scene.c callouts keeps a copy).

With VerzatileDev's controller pack in new-assets/controller-diagrams (CC BY
4.0, but its files may not be redistributed, so it is not in the
repository), pad() assembles the "UI TendoTendo" controller from its separate
parts (UI_TendoTendo_Seperate: body, shoulder buttons, D-pad, Start, A, B,
C buttons, stick cap) where the pack's assembled UI_TendoTendoThumbnail1.png
has them, scales it down, cuts alpha to 1 bit (RGBA16) and outlines the
silhouette like the rest of the art. Without it, pad() draws a simpler
controller with the parts at the same places.

`python3 tools/controls.py out.png` writes the picture at 4:3 (2x across,
4x down).
"""
import os

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.path.join(ROOT, "new-assets", "controller-diagrams", "UI_TendoTendo_Seperate")

W, H = 232, 108                 # sim px
OUTLINE = (20, 16, 36, 255)

# name -> (x, y, r), art px.
PARTS = {
    "START": (58, 41, 5),
    "DPAD": (23, 36, 10),
    "STICK": (58, 65, 9),
    "Z": (58, 88, 4),
    "R": (104, 10, 3),
    "A": (85, 46, 4),
    "B": (78, 37, 4),
}

# ---- the pack's controller --------------------------------------------------

SHARPEN = 80                    # unsharp mask after scaling down, percent
# Source px of the body (g231, 2079x1995); every part is at the body's scale.
BODY = "g231"
SHOULDER_L, SHOULDER_R = ("g235", (144, 116)), ("g234", (1440, 108))
# Part centers, from the thumbnail (scaled 2554/2079 there, body at 906,659).
DPAD = ("g158", (409, 660))     # g158 nothing pressed; g98/g122/g132/g148 one arrow lit
START = ("g232", (1043, 746))   # g233 darker
B_BTN = ("g238", (1403, 669))   # g239 darker
A_BTN = ("g237", (1529, 838))   # g236 darker
C_BTNS = (1721, 584)            # no unlit set in the pack: g246's left half + g245's right
STICK = ("g244", (1042, 1207))
Z_BTN = (1042, 1640)            # trigger behind the middle grip
R_BTN = (1880, 160)             # visible end of the R shoulder
# PARTS in body source px: name -> (center, radius).
CALLOUTS = {
    "START": (START[1], 90),
    "DPAD": (DPAD[1], 180),
    "STICK": (STICK[1], 160),
    "Z": (Z_BTN, 80),
    "R": (R_BTN, 60),
    "A": (A_BTN[1], 84),
    "B": (B_BTN[1], 84),
}


def load(name):
    return Image.open(os.path.join(PACK, f"UI_SuperNinNotTendoSVG_{name}.png")).convert("RGBA")


def c_buttons():
    lit_left, lit_right = load("g245"), load("g246")
    half = lit_left.width // 2
    out = lit_left.copy()
    out.paste(lit_right.crop((0, 0, half, lit_right.height)), (0, 0))
    return out


def assemble():
    """The whole controller in body source px."""
    body = load(BODY)
    im = Image.new("RGBA", body.size)
    for name, at in (SHOULDER_L, SHOULDER_R):
        im.alpha_composite(load(name), at)
    im.alpha_composite(body)
    for part, (cx, cy) in ((load(DPAD[0]), DPAD[1]), (load(START[0]), START[1]),
                           (load(B_BTN[0]), B_BTN[1]), (load(A_BTN[0]), A_BTN[1]),
                           (c_buttons(), C_BTNS), (load(STICK[0]), STICK[1])):
        im.alpha_composite(part, (cx - part.width // 2, cy - part.height // 2))
    return im


def pack_pad():
    """Scaled into W x H with a 1 px outline margin (2 across)."""
    im = assemble()
    box = im.getbbox()
    sx, sy = (W - 4) / (box[2] - box[0]), (H - 2) / (box[3] - box[1])
    parts = {name: (round((2 + (cx - box[0]) * sx) / 2), round(1 + (cy - box[1]) * sy),
                    round(r * sy))
             for name, ((cx, cy), r) in CALLOUTS.items()}
    if parts != PARTS:
        raise SystemExit(f"controls: parts moved, update PARTS and scene.c callouts: {parts}")
    small = im.crop(box).resize((W - 4, H - 2), Image.LANCZOS)
    alpha = small.getchannel("A")
    small = small.convert("RGB").filter(ImageFilter.UnsharpMask(1, SHARPEN, 0))
    small.putalpha(alpha)
    out = Image.new("RGBA", (W, H))
    out.paste(small, (2, 1))
    px = out.load()
    solid = [[px[x, y][3] >= 128 for x in range(W)] for y in range(H)]
    for y in range(H):
        for x in range(W):
            if solid[y][x]:
                px[x, y] = px[x, y][:3] + (255,)
            elif any(0 <= x + dx < W and 0 <= y + dy < H and solid[y + dy][x + dx]
                     for dx, dy in ((-2, 0), (-1, 0), (1, 0), (2, 0), (0, -1), (0, 1))):
                px[x, y] = OUTLINE
            else:
                px[x, y] = (0, 0, 0, 0)
    return out

# ---- drawn controller (without the pack), art px, stretched 2x across -------

AW = W // 2
BODY_FILL = (178, 180, 192, 255)
BODY_LIGHT = (208, 210, 220, 255)
BODY_DARK = (132, 134, 150, 255)
DARK = (64, 64, 78, 255)
DARK_LIGHT = (98, 98, 114, 255)
RED = (214, 52, 52, 255)
BLUE = (60, 104, 224, 255)
GREEN = (52, 168, 76, 255)
YELLOW = (240, 200, 40, 255)
C_CENTER = (95, 32)


def disc(d, c, r, fill, outline=OUTLINE):
    x, y = c
    d.ellipse((x - r, y - r, x + r, y + r), fill=fill, outline=outline)


def body_mask():
    """Silhouette: a wide top with rounded ends and three grips below."""
    im = Image.new("1", (AW, H))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((2, 8, AW - 3, 58), radius=18, fill=1)
    d.rounded_rectangle((44, 40, 72, H - 3), radius=14, fill=1)          # middle grip
    for x0, x1, top in ((4, 30, 50), (AW - 31, AW - 5, 50)):
        d.rounded_rectangle((x0, top, x1, 92), radius=13, fill=1)
    d.polygon([(10, 50), (40, 50), (30, 80), (6, 80)], fill=1)            # left grip slants out
    d.polygon([(AW - 41, 50), (AW - 11, 50), (AW - 7, 80), (AW - 31, 80)], fill=1)
    return im


def drawn_pad():
    im = Image.new("RGBA", (AW, H))
    d = ImageDraw.Draw(im)
    rx, ry, _ = PARTS["R"]
    d.rounded_rectangle((AW - 1 - rx - 12, ry - 6, AW - 1 - rx + 18, ry + 3), radius=3,
                        fill=DARK, outline=OUTLINE)                       # L
    d.rounded_rectangle((rx - 18, ry - 6, rx + 12, ry + 3), radius=3, fill=DARK, outline=OUTLINE)
    mask = body_mask()
    m = mask.load()
    px = im.load()
    for y in range(H):
        for x in range(AW):
            if not m[x, y]:
                continue
            edge = any(not (0 <= x + dx < AW and 0 <= y + dy < H and m[x + dx, y + dy])
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if edge:
                px[x, y] = OUTLINE
            elif not m[x, y - 2]:
                px[x, y] = BODY_LIGHT
            elif y + 3 < H and not m[x, y + 3]:
                px[x, y] = BODY_DARK
            else:
                px[x, y] = BODY_FILL
    zx, zy, _ = PARTS["Z"]          # Z trigger (really under the middle grip), seen through it
    d.rounded_rectangle((zx - 7, zy - 4, zx + 7, zy + 4), radius=3, fill=DARK, outline=OUTLINE)
    x, y, _ = PARTS["DPAD"]
    d.rectangle((x - 3, y - 10, x + 3, y + 10), fill=DARK, outline=OUTLINE)
    d.rectangle((x - 10, y - 3, x + 10, y + 3), fill=DARK, outline=OUTLINE)
    d.rectangle((x - 2, y - 9, x + 2, y + 9), fill=DARK)
    d.rectangle((x - 9, y - 2, x + 9, y + 2), fill=DARK)
    d.point([(x - 1, y - 8), (x - 8, y - 1)], fill=DARK_LIGHT)
    disc(d, PARTS["START"][:2], 3, RED)
    sx, sy, _ = PARTS["STICK"]
    disc(d, (sx, sy), 9, DARK)
    disc(d, (sx, sy), 5, BODY_LIGHT)
    d.point([(sx - 2, sy - 2), (sx - 1, sy - 3)], fill=(240, 240, 248, 255))
    disc(d, PARTS["B"][:2], 4, GREEN)
    disc(d, PARTS["A"][:2], 5, BLUE)
    cx, cy = C_CENTER
    for dx, dy in ((0, -7), (-7, 0), (7, 0), (0, 7)):
        disc(d, (cx + dx, cy + dy), 3, YELLOW)
    for name, r in (("B", 4), ("A", 5), ("START", 3)):
        bx, by, _ = PARTS[name]
        d.point([(bx - r // 2, by - r // 2)], fill=(255, 255, 255, 200))
    return im.resize((W, H), Image.NEAREST)


def pad():
    return pack_pad() if os.path.isdir(PACK) else drawn_pad()


if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "pad.png"
    p = drawn_pad() if "--drawn" in sys.argv else pad()
    p.resize((p.width * 2, p.height * 4), Image.NEAREST).save(out)
