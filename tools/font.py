"""Display font: chunky capitals for the title, pause menu and winner banner.

Glyphs are drawn here at 9 art px tall with 2 px strokes, then smoothed
up 2x (Scale2x) and 3x (Scale3x), so diagonals and corners come out
rounded. The small size (labels on the CONTROLS screen) is Roundabout by
FeatureKeep (CC0, from libdragon's font gallery example) at its native
12 px, assets/src/fonts/roundabout.ttf. Each glyph gets a grey gradient
fill (white top, darker bottom), a 1 px dark outline and a drop shadow; the
renderer tints the fill per use (platform/common/scene.c font_text), so one
sheet serves every color.

sheet() returns the sheet in art px (build_sprites.py stretches it 2x
wide like every sheet) and header() the C metrics scene.c reads, written
to platform/common/font_glyphs.h.
"""
import os

from PIL import Image, ImageDraw, ImageFont

GLYPH_H = 9
# Per size: scale of the drawn glyphs (0 = Roundabout), glyphs per sheet
# row (sheet <= 1024 sim px wide). FONT_MEDIUM (menu items, hints),
# FONT_LARGE (headlines), FONT_SMALL (labels).
SIZES = ((2, 16), (3, 12), (0, 24))
SMALL_TTF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "assets", "src", "fonts", "roundabout.ttf")
SMALL_PX = 12
OUTLINE = (20, 16, 36, 255)
SHADOW = (0, 0, 0, 255)

GLYPHS = {
    " ": ["...", "...", "...", "...", "...", "...", "...", "...", "..."],
    "!": ["##", "##", "##", "##", "##", "##", "..", "##", "##"],
    "'": ["##", "##", "##", "..", "..", "..", "..", "..", ".."],
    "+": ["......", "..##..", "..##..", "######", "######", "..##..", "..##..", "......",
          "......"],
    ",": ["...", "...", "...", "...", "...", "...", ".##", ".##", "##."],
    "/": [".....##", "....###", "....##.", "...###.", "..###..", ".###...", ".##....", "###....",
          "##....."],
    "-": ["......", "......", "......", "######", "######", "......", "......", "......", "......"],
    ".": ["..", "..", "..", "..", "..", "..", "..", "##", "##"],
    ":": ["..", "..", "##", "##", "..", "..", "##", "##", ".."],
    ">": ["##....", "###...", ".###..", "..###.", "...###", "..###.", ".###..", "###...", "##...."],
    "?": [".######.", "########", "##....##", ".....###", "...####.", "...##...", "........",
          "...##...", "...##..."],
    "0": ["..####..", ".######.", "###..###", "##..####", "##.##.##", "####..##", "###..###",
          ".######.", "..####.."],
    "1": ["..##", ".###", "####", "..##", "..##", "..##", "..##", "..##", "..##"],
    "2": [".######.", "########", "##....##", "......##", "..#####.", ".#####..", "###.....",
          "########", "########"],
    "3": [".######.", "########", "......##", "..#####.", "..#####.", "......##", "......##",
          "########", ".######."],
    "4": ["##....##", "##....##", "##....##", "########", "########", "......##", "......##",
          "......##", "......##"],
    "5": ["########", "########", "##......", "#######.", "########", "......##", "......##",
          "########", ".######."],
    "6": [".######.", "########", "##......", "#######.", "########", "##....##", "##....##",
          "########", ".######."],
    "7": ["########", "########", "......##", ".....###", "....###.", "...###..", "...##...",
          "...##...", "...##..."],
    "8": [".######.", "########", "##....##", ".######.", ".######.", "##....##", "##....##",
          "########", ".######."],
    "9": [".######.", "########", "##....##", "##....##", "########", ".#######", "......##",
          "########", ".######."],
    "A": ["..####..", ".######.", "###..###", "##....##", "##....##", "########", "########",
          "##....##", "##....##"],
    "B": ["######..", "#######.", "##...##.", "######..", "#######.", "##...###", "##....##",
          "########", "#######."],
    "C": ["..#####.", ".#######", "###....#", "##......", "##......", "##......", "###....#",
          ".#######", "..#####."],
    "D": ["######..", "#######.", "##...###", "##....##", "##....##", "##....##", "##...###",
          "#######.", "######.."],
    "E": ["#######", "#######", "##.....", "######.", "######.", "##.....", "##.....",
          "#######", "#######"],
    "F": ["#######", "#######", "##.....", "######.", "######.", "##.....", "##.....",
          "##.....", "##....."],
    "G": ["..#####.", ".#######", "###....#", "##......", "##..####", "##..####", "###...##",
          ".#######", "..#####."],
    "H": ["##....##", "##....##", "##....##", "########", "########", "##....##", "##....##",
          "##....##", "##....##"],
    "I": ["##", "##", "##", "##", "##", "##", "##", "##", "##"],
    "J": [".....##", ".....##", ".....##", ".....##", ".....##", "##...##", "###.###",
          ".#####.", "..###.."],
    "K": ["##...###", "##..###.", "##.###..", "#####...", "####....", "#####...", "##.###..",
          "##..###.", "##...###"],
    "L": ["##.....", "##.....", "##.....", "##.....", "##.....", "##.....", "##.....",
          "#######", "#######"],
    "M": ["##......##", "###....###", "####..####", "##.####.##", "##..##..##", "##......##",
          "##......##", "##......##", "##......##"],
    "N": ["##....##", "###...##", "####..##", "#####.##", "##.#####", "##..####", "##...###",
          "##....##", "##....##"],
    "O": ["..####..", ".######.", "###..###", "##....##", "##....##", "##....##", "###..###",
          ".######.", "..####.."],
    "P": ["######..", "#######.", "##...###", "##....##", "##...###", "#######.", "######..",
          "##......", "##......"],
    "Q": ["..####..", ".######.", "###..###", "##....##", "##....##", "##.##.##", "###.####",
          ".######.", "..###.##"],
    "R": ["######..", "#######.", "##...###", "##....##", "##...###", "#######.", "######..",
          "##..###.", "##...###"],
    "S": [".######.", "########", "##......", "#######.", ".#######", "......##", "......##",
          "########", ".######."],
    "T": ["########", "########", "...##...", "...##...", "...##...", "...##...", "...##...",
          "...##...", "...##..."],
    "U": ["##....##", "##....##", "##....##", "##....##", "##....##", "##....##", "###..###",
          ".######.", "..####.."],
    "V": ["##....##", "##....##", "##....##", "##....##", "###..###", ".##..##.", ".######.",
          "..####..", "...##..."],
    "W": ["##......##", "##......##", "##......##", "##..##..##", "##.####.##", "##########",
          "####..####", "###....###", "##......##"],
    "X": ["##....##", "###..###", ".######.", "..####..", "...##...", "..####..", ".######.",
          "###..###", "##....##"],
    "Y": ["##....##", "###..###", ".######.", "..####..", "...##...", "...##...", "...##...",
          "...##...", "...##..."],
    "Z": ["########", "########", ".....###", "....###.", "...###..", "..###...", ".###....",
          "########", "########"],
}
CHARS = "".join(sorted(GLYPHS))


def bitmap(rows):
    assert len(rows) == GLYPH_H and len({len(r) for r in rows}) == 1, rows
    return [[c == "#" for c in r] for r in rows]


def scale2x(src):
    h, w = len(src), len(src[0])

    def at(x, y):
        return src[y][x] if 0 <= x < w and 0 <= y < h else False

    out = [[False] * (w * 2) for _ in range(h * 2)]
    for y in range(h):
        for x in range(w):
            a, b, c, d, p = at(x, y - 1), at(x + 1, y), at(x - 1, y), at(x, y + 1), at(x, y)
            out[2 * y][2 * x] = a if c == a and c != d and a != b else p
            out[2 * y][2 * x + 1] = b if a == b and a != c and b != d else p
            out[2 * y + 1][2 * x] = c if d == c and d != b and c != a else p
            out[2 * y + 1][2 * x + 1] = d if b == d and b != a and d != c else p
    return out


def scale3x(src):
    h, w = len(src), len(src[0])

    def at(x, y):
        return src[y][x] if 0 <= x < w and 0 <= y < h else False

    out = [[False] * (w * 3) for _ in range(h * 3)]
    for y in range(h):
        for x in range(w):
            A, B, C = at(x - 1, y - 1), at(x, y - 1), at(x + 1, y - 1)
            D, E, F = at(x - 1, y), at(x, y), at(x + 1, y)
            G, H, I = at(x - 1, y + 1), at(x, y + 1), at(x + 1, y + 1)
            e = [E] * 9
            if B != H and D != F:
                e[0] = D if D == B else E
                e[1] = B if (D == B and E != C) or (B == F and E != A) else E
                e[2] = F if B == F else E
                e[3] = D if (D == B and E != G) or (D == H and E != A) else E
                e[5] = F if (B == F and E != I) or (H == F and E != C) else E
                e[6] = D if D == H else E
                e[7] = H if (D == H and E != I) or (H == F and E != G) else E
                e[8] = F if H == F else E
            for i, v in enumerate(e):
                out[3 * y + i // 3][3 * x + i % 3] = v
    return out


def small_glyphs():
    """Roundabout glyphs as bitmaps cropped to their ink, sharing a cap line
    (row 0) and a height that fits the comma's tail."""
    f = ImageFont.truetype(SMALL_TTF, SMALL_PX)
    top = f.getbbox("A")[1]
    bottom = max(f.getbbox(c)[3] for c in CHARS if c != " ")
    out = {}
    for c in CHARS:
        adv = round(f.getlength(c))
        im = Image.new("1", (adv + 4, bottom - top))
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((0, -top), c, font=f, fill=1)
        box = im.getbbox()
        if box is None:     # space: its advance, blank
            out[c] = [[False] * max(adv - 2, 1) for _ in range(im.height)]
            continue
        px = im.load()
        out[c] = [[bool(px[x, y]) for x in range(box[0], box[2])] for y in range(im.height)]
    return out


def glyph_bits(size):
    s = SIZES[size][0]
    if s == 0:
        return small_glyphs()
    up = scale2x if s == 2 else scale3x
    return {c: up(bitmap(GLYPHS[c])) for c in CHARS}


def shadow(size):
    return max(SIZES[size][0] - 1, 1)


def render(bits, sh):
    """Glyph image: fill offset (1, 1) for the outline, shadow sh px
    down-right."""
    h, w = len(bits), len(bits[0])
    im = Image.new("RGBA", (w + 2 + sh, h + 2 + sh))
    px = im.load()

    def filled(x, y):
        return 0 <= x < w and 0 <= y < h and bits[y][x]

    def near(x, y):     # fill or outline at image pixel (x, y)
        return any(filled(x - 1 + dx, y - 1 + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1))

    for y in range(im.height):
        for x in range(im.width):
            if near(x - sh, y - sh):
                px[x, y] = SHADOW
    for y in range(im.height):
        for x in range(im.width):
            if filled(x - 1, y - 1):
                t = (y - 1) / (h - 1)
                v = 255 if t < 0.4 else round(255 - (t - 0.4) / 0.6 * 95)
                px[x, y] = (v, v, v, 255)
            elif near(x, y):
                px[x, y] = OUTLINE
    return im


def cell(bits, sh):
    """Sheet cell (w, h) in art px for a size: the widest glyph."""
    w = max(len(g[0]) for g in bits.values())
    return w + 2 + sh, len(bits["A"]) + 2 + sh


def sheet():
    parts = []
    for size, (_, cols) in enumerate(SIZES):
        bits, sh = glyph_bits(size), shadow(size)
        cw, chh = cell(bits, sh)
        rows = (len(CHARS) + cols - 1) // cols
        part = Image.new("RGBA", (cols * cw, rows * chh))
        for i, ch in enumerate(CHARS):
            part.paste(render(bits[ch], sh), (i % cols * cw, i // cols * chh))
        parts.append(part)
    out = Image.new("RGBA", (max(p.width for p in parts), sum(p.height for p in parts)))
    y = 0
    for p in parts:
        out.paste(p, (0, y))
        y += p.height
    return out


def header():
    lines = [
        "/* Generated by tools/build_sprites.py from tools/font.py: display font",
        " * sheet layout (\"font\" sprite), in sim px (art px x 2 wide). */",
        "#ifndef FONT_GLYPHS_H",
        "#define FONT_GLYPHS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define FONT_CHARS \"{CHARS}\"",
        "",
        "/* Per size: fill height (art px), shadow (art px), glyphs per row,",
        " * first row y, cell w and h. */",
        f"static const struct {{ uint8_t h, shadow, cols; uint16_t y, cell_w, cell_h; }} "
        f"font_sizes[{len(SIZES)}] = {{",
    ]
    widths = []
    y = 0
    for size, (_, cols) in enumerate(SIZES):
        bits, sh = glyph_bits(size), shadow(size)
        cw, chh = cell(bits, sh)
        lines.append(f"    {{{len(bits['A'])}, {sh}, {cols}, {y}, {cw * 2}, {chh}}},")
        y += (len(CHARS) + cols - 1) // cols * chh
        widths.append("    {" + ", ".join(str(len(bits[c][0])) for c in CHARS) + "},")
    lines += ["};", "",
              "/* Glyph fill width in art px, per size and FONT_CHARS entry. */",
              f"static const uint8_t font_glyph_w[{len(SIZES)}][{len(CHARS)}] = {{"]
    lines += widths + ["};", "", "#endif", ""]
    return "\n".join(lines)
