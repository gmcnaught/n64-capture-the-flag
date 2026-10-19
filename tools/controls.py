"""N64 controller picture for the pause menu's CONTROLS screen.

pad() returns it in art px (build_sprites.py stretches it 2x wide like
every sheet): the controller from the front, three grips, with the parts
the game uses in their console colors. The renderer draws labels and
leader lines to the parts at the PAD_* points below (art px from the
picture's top-left; platform/common/scene.c draw_controls keeps a copy).
"""
from PIL import Image, ImageDraw

W, H = 128, 84
OUTLINE = (24, 22, 36, 255)
BODY = (178, 180, 192, 255)
BODY_LIGHT = (208, 210, 220, 255)
BODY_DARK = (132, 134, 150, 255)
DARK = (64, 64, 78, 255)
DARK_LIGHT = (98, 98, 114, 255)
RED = (214, 52, 52, 255)
BLUE = (60, 104, 224, 255)
GREEN = (52, 168, 76, 255)
YELLOW = (240, 200, 40, 255)

# Parts (centers), art px.
DPAD = (24, 30)
START = (64, 26)
STICK = (64, 52)
B_BTN = (90, 34)
A_BTN = (98, 44)
C_BTNS = (110, 26)      # center of the four C buttons
L_BTN = (20, 5)
R_BTN = (108, 5)
Z_BTN = (64, 72)        # trigger behind the middle grip, drawn below the stick


def disc(d, c, r, fill, outline=OUTLINE):
    x, y = c
    d.ellipse((x - r, y - r, x + r, y + r), fill=fill, outline=outline)


def body_mask():
    """Silhouette: a wide top with rounded ends and three grips below."""
    im = Image.new("1", (W, H))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((2, 7, W - 3, 44), radius=14, fill=1)
    for cx in (22, 64, 106):
        d.rounded_rectangle((cx - 13, 30, cx + 13, 70 if cx != 64 else 66), radius=12, fill=1)
    d.polygon([(16, 40), (40, 40), (34, 64), (14, 64)], fill=1)          # left grip slants out
    d.polygon([(88, 40), (112, 40), (114, 64), (94, 64)], fill=1)
    return im


def pad():
    im = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(im)
    # Shoulder buttons behind the top edge.
    d.rounded_rectangle((8, 2, 34, 10), radius=3, fill=DARK, outline=OUTLINE)
    d.rounded_rectangle((94, 2, 120, 10), radius=3, fill=DARK, outline=OUTLINE)
    # Body: outline ring, fill, a light top edge and a darker bottom.
    mask = body_mask()
    m = mask.load()
    px = im.load()
    for y in range(H):
        for x in range(W):
            if not m[x, y]:
                continue
            edge = any(not (0 <= x + dx < W and 0 <= y + dy < H and m[x + dx, y + dy])
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if edge:
                px[x, y] = OUTLINE
            elif not m[x, y - 2]:
                px[x, y] = BODY_LIGHT
            elif y + 3 < H and not m[x, y + 3]:
                px[x, y] = BODY_DARK
            else:
                px[x, y] = BODY
    # Z trigger (really under the middle grip), seen through it.
    d.rounded_rectangle((Z_BTN[0] - 7, Z_BTN[1] - 4, Z_BTN[0] + 7, Z_BTN[1] + 4), radius=3,
                        fill=DARK, outline=OUTLINE)
    # D-pad.
    x, y = DPAD
    d.rectangle((x - 3, y - 9, x + 3, y + 9), fill=DARK, outline=OUTLINE)
    d.rectangle((x - 9, y - 3, x + 9, y + 3), fill=DARK, outline=OUTLINE)
    d.rectangle((x - 2, y - 8, x + 2, y + 8), fill=DARK)
    d.rectangle((x - 8, y - 2, x + 8, y + 2), fill=DARK)
    d.point([(x - 1, y - 7), (x - 7, y - 1)], fill=DARK_LIGHT)
    # Start.
    disc(d, START, 3, RED)
    # Analog stick: well, then the cap.
    disc(d, STICK, 9, DARK)
    disc(d, STICK, 5, BODY_LIGHT)
    d.point([(STICK[0] - 2, STICK[1] - 2), (STICK[0] - 1, STICK[1] - 3)], fill=(240, 240, 248, 255))
    # B, A, then the C buttons.
    disc(d, B_BTN, 5, GREEN)
    disc(d, A_BTN, 6, BLUE)
    cx, cy = C_BTNS
    for dx, dy in ((0, -6), (-6, 0), (6, 0), (0, 6)):
        disc(d, (cx + dx, cy + dy), 3, YELLOW)
    # A little shine on the colored buttons.
    for (bx, by), r in ((B_BTN, 5), (A_BTN, 6), (START, 3)):
        d.point([(bx - r // 2, by - r // 2)], fill=(255, 255, 255, 200))
    return im


if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "pad.png"
    p = pad()
    p.resize((p.width * 4, p.height * 4), Image.NEAREST).save(out)
