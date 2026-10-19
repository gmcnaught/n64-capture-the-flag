"""Terrain tiles: every solid-cell neighborhood, built from quarter tiles.

A solid cell's look depends on its 8 neighbors. Each quarter of the cell
(top-left, top-right, bottom-left, bottom-right) depends only on the two
side neighbors next to it and the diagonal between them:
  vertical open, horizontal open      outer corner
  vertical open only                  top / bottom edge
  horizontal open only                left / right wall
  both solid, diagonal open           inner corner
  all solid                           fill
That is 47 distinct cells ("blob" autotiling: a diagonal counts only when
both sides next to it are solid). Each quarter is copied from the same
quarter of a source tile (PARTS), so a full sheet comes from the nine
tiles of a 3x3 block plus the inner corners. Cells open above and below
use the source's one-high slab tiles when it has them. Fully solid
cells pick among fill variants (FILLS) by position, so the fill does not
repeat on a grid.

Sheets ("tiles_<tileset>", one per TILESETS entry; the level header's
tileset byte is the index) are in sim px, 16x8 per cell, BLOB_COLS cells
per row: the blob tiles in blob_masks() order, the one-way platform tiles
ONEWAY_*, the fill variants from FILL_FIRST, and from EXTRA_FIRST the
ladder and spike tiles of arena.png (LADDER_*, SPIKES), the same in
every sheet, and from ICE_FIRST an ice-colored copy of the first
EXTRA_FIRST (cells with SURFACE_ICE, core/level.h). tile_index() is a
cell's tile, used by tools/ldtk_import.py to write each level's tile
layer. Sources:
  arena   assets/src/tilesets/arena.png, 8x8 art px tiles, each quarter
          4x4 art px, stretched 2x wide
  rocky_* Rocky Roads 16x16 tilesets at full width and 8 of 16 rows. One
          row choice per tileset (rows 0-7 -> 4, 8-15 -> 4; pick_rows),
          so every quarter keeps the same rows and edges line up.

header() is platform/common/tilesets.h: sheet count and columns.

python3 tools/terrain.py OUT_DIR writes each sheet and an arena preview
per tileset (build/assets/levels/arena.lvl, build/assets/n64 backdrops)
to OUT_DIR.
"""
import colorsys
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROCKY = os.path.join(ROOT, "new-assets", "RockyRoads", "Rocky Roads", "Tilesets")
ARENA = os.path.join(ROOT, "assets", "src", "tilesets", "arena.png")

CELL_W, CELL_H = 16, 8
BLOB_COLS = 16
N, E, S, W, NE, SE, SW, NW = (1 << i for i in range(8))

# Source tiles by role: (col, row) in the tileset. TL..BR: the 3x3 block
# of a large solid; ITL/ITR/IBL/IBR: inner corners, the open diagonal at
# that corner (bottom ones are drawn when missing: inner_bottom); slab
# (left, middle, right) and single: one cell high, open above and below;
# fills: variants for fully solid cells (F, the fill of every other
# quarter, is the plain one).
ROCKY_LAYOUT = dict(TL=(1, 2), T=(3, 1), TR=(6, 2), L=(1, 3), F=(4, 3), R=(6, 3),
                    BL=(1, 4), B=(3, 4), BR=(6, 4), ITL=(2, 2), ITR=(5, 2),
                    slab=[(8, 3), (9, 3), (10, 3)], single=(8, 1),
                    fills=[(3, 3), (2, 3), (3, 2), (4, 2), (5, 3)])
NARROW_LAYOUT = dict(TL=(1, 2), T=(3, 1), TR=(5, 2), L=(1, 3), F=(3, 3), R=(5, 3),
                     BL=(1, 4), B=(3, 4), BR=(5, 4), ITL=(2, 2), ITR=(4, 2))
TILESETS = {
    "arena": (ARENA, 8, dict(TL=18, T=19, TR=20, L=35, F=36, R=37, BL=52, B=53, BR=54,
                             ITL=58, ITR=56, IBL=24, IBR=22, slab=[88, 89, 90], single=86,
                             oneway=[49, 66, 83], oneway_single=32)),
    "rocky_forest": (os.path.join(ROCKY, "tileset_forest.png"), 16, ROCKY_LAYOUT),
    "rocky_desert": (os.path.join(ROCKY, "tileset_desert.png"), 16, ROCKY_LAYOUT),
    "rocky_snow": (os.path.join(ROCKY, "tileset_snow.png"), 16,
                   dict(NARROW_LAYOUT, slab=[(7, 3), (8, 3), (9, 3)], single=(7, 1))),
    "rocky_lava": (os.path.join(ROCKY, "tileset_lava.png"), 16, NARROW_LAYOUT),
}


def blob_masks():
    """The 47 neighbor masks (bit set = solid), diagonals kept only where
    both sides next to them are solid, in sheet order."""
    out = set()
    for m in range(256):
        for d, a, b in ((NE, N, E), (SE, S, E), (SW, S, W), (NW, N, W)):
            if not (m & a and m & b):
                m &= ~d
        out.add(m)
    return sorted(out)


def blob_mask(solid, x, y):
    """Canonical mask of cell (x, y); solid(x, y) -> bool."""
    m = 0
    for bit, dx, dy in ((N, 0, -1), (E, 1, 0), (S, 0, 1), (W, -1, 0),
                        (NE, 1, -1), (SE, 1, 1), (SW, -1, 1), (NW, -1, -1)):
        if solid(x + dx, y + dy):
            m |= bit
    for d, a, b in ((NE, N, E), (SE, S, E), (SW, S, W), (NW, N, W)):
        if not (m & a and m & b):
            m &= ~d
    return m


class Source:
    def __init__(self, name):
        path, self.t, self.parts = TILESETS[name]
        self.sheet = Image.open(path).convert("RGBA")
        self.cols = self.sheet.width // self.t

    def tile(self, ref):
        c, r = ref if isinstance(ref, tuple) else (ref % self.cols, ref // self.cols)
        return self.sheet.crop((c * self.t, r * self.t, c * self.t + self.t, r * self.t + self.t))

    def quarter(self, ref, qx, qy):
        h = self.t // 2
        return self.tile(ref).crop((qx * h, qy * h, qx * h + h, qy * h + h))

    def inner(self, qx, qy):
        """Inner-corner quarter, open diagonal at corner (qx, qy)."""
        role = "I" + "TB"[qy] + "LR"[qx]
        if role in self.parts:
            return self.quarter(self.parts[role], qx, qy)
        return inner_bottom(self, qx)

    def cell(self, m):
        """Source-resolution tile for blob mask m."""
        p, t, h = self.parts, self.t, self.t // 2
        if not m & N and not m & S and "slab" in p:
            lo, mid, ro = p["slab"]
            return self.tile(p["single"] if not m & (E | W) else
                             lo if not m & W else ro if not m & E else mid)
        out = Image.new("RGBA", (t, t))
        for qy, v, vs in ((0, N, "T"), (1, S, "B")):
            for qx, hz, hs in ((0, W, "L"), (1, E, "R")):
                diag = {(0, 0): NW, (1, 0): NE, (0, 1): SW, (1, 1): SE}[qx, qy]
                if not m & v and not m & hz:
                    q = self.quarter(p[vs + hs], qx, qy)
                elif not m & v:
                    q = self.quarter(p[vs], qx, qy)
                elif not m & hz:
                    q = self.quarter(p[hs], qx, qy)
                elif not m & diag:
                    q = self.inner(qx, qy)
                else:
                    q = self.quarter(p["F"], qx, qy)
                out.paste(q, (qx * h, qy * h))
        return out

    def oneway(self, i):
        """One-way platform tile: 0 left end, 1 middle, 2 right end, 3 single."""
        p = self.parts
        if "oneway" in p:
            return self.tile((p["oneway"] + [p["oneway_single"]])[i])
        return self.cell([E, E | W, W, 0][i])


def inner_bottom(src, qx):
    """Bottom inner corner, for tilesets without one: fill, plus the rocks
    of the bottom edge nearest the open corner and of the wall above it,
    as where a bottom edge meets a wall."""
    p, h = src.parts, src.t // 2
    q = src.quarter(p["F"], qx, 1)
    fill = q.getpixel((h // 2, h // 2))
    edge, wall = src.quarter(p["B"], qx, 1), src.quarter(p["LR"[qx]], qx, 1)
    k = h * 3 // 8
    for y in range(h):
        for x in range(h):
            cx = x if qx == 0 else h - 1 - x
            for part, near in ((edge, cx < k), (wall, y >= h - k)):
                c = part.getpixel((x, y))
                if near and c[3] >= 128 and c != fill:
                    q.putpixel((x, y), c)
    return q


def row_cost(a, b):
    """Alpha-aware RGB distance between two pixel rows."""
    s = 0
    for p, q in zip(a, b):
        if (p[3] >= 128) != (q[3] >= 128):
            s += 3 * 255
        elif p[3] >= 128:
            s += abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2])
    return s


def pick_half(rows, lam=0.6):
    """4 of the 8 rows of a half tile: each kept row stands for a pair of
    source rows; lam rewards contrast between kept rows, so 1 px lines
    (outlines, mortar) are kept instead of averaged away."""
    n, k = len(rows), len(rows) // 2
    cost = [[row_cost(rows[2 * i], rows[r]) + row_cost(rows[2 * i + 1], rows[r])
             for r in range(n)] for i in range(k)]
    INF = float("inf")
    best = [[INF] * n for _ in range(k)]
    back = [[-1] * n for _ in range(k)]
    best[0] = cost[0][:]
    for i in range(1, k):
        for r in range(i, n):
            for j in range(i - 1, r):
                if best[i - 1][j] < INF:
                    v = best[i - 1][j] + cost[i][r] - lam * row_cost(rows[j], rows[r])
                    if v < best[i][r]:
                        best[i][r], back[i][r] = v, j
    r = min(range(n), key=lambda r: best[k - 1][r])
    ks = [r]
    for i in range(k - 1, 0, -1):
        r = back[i][r]
        ks.append(r)
    return ks[::-1]


def pick_rows(tiles):
    """Rows of a 16-row tile to keep, chosen over all the given tiles side
    by side: 4 from rows 0-7 and 4 from rows 8-15."""
    t = tiles[0].height
    strip = Image.new("RGBA", (t * len(tiles), t))
    for i, im in enumerate(tiles):
        strip.paste(im, (i * t, 0))
    px = strip.load()
    rows = [[px[x, y] for x in range(strip.width)] for y in range(t)]
    h = t // 2
    return pick_half(rows[:h]) + [h + r for r in pick_half(rows[h:])]


def to_cell(im, keep):
    """Source tile -> 16x8 sim px cell."""
    if keep is None:  # 8x8 art px, stretched 2x wide
        return im.resize((CELL_W, CELL_H), Image.NEAREST)
    out = Image.new("RGBA", (CELL_W, CELL_H))
    for i, r in enumerate(keep):
        out.paste(im.crop((0, r, CELL_W, r + 1)), (0, i))
    return out


ONEWAY_LEFT, ONEWAY_MID, ONEWAY_RIGHT, ONEWAY_SINGLE = range(47, 51)
FILL_FIRST = 51
EXTRA_FIRST = 4 * BLOB_COLS
LADDER_TOP, LADDER_MID, LADDER_BOTTOM, SPIKES = range(EXTRA_FIRST, EXTRA_FIRST + 4)
EXTRAS = [204, 205, 206, 207]           # arena.png ids, in LADDER_TOP.. order
ICE_FIRST = EXTRA_FIRST + BLOB_COLS
SURFACE_ICE = 1                         # core/level.h SURFACE_ICE


def icy(im):
    """Ice colors: every pixel pale cyan, as bright as it was (brighter
    pixels less saturated)."""
    out = im.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                v = min(1.0, 0.35 + 0.75 * max(r, g, b) / 255)
                r, g, b = colorsys.hsv_to_rgb(0.52, 0.6 * (1.15 - v), v)
                px[x, y] = (round(r * 255), round(g * 255), round(b * 255), a)
    return out


def sheet(name):
    """The 16x8 sim px sheet (layout: module doc)."""
    src = Source(name)
    masks = blob_masks()
    tiles = [src.cell(m) for m in masks] + [src.oneway(i) for i in range(4)]
    tiles += [src.tile(ref) for ref in src.parts.get("fills", [])]
    assert len(tiles) <= EXTRA_FIRST, name
    keep = pick_rows(tiles) if src.t == 16 else None
    tiles = [to_cell(im, keep) for im in tiles]
    tiles += [None] * (EXTRA_FIRST - len(tiles))
    arena = Source("arena")
    tiles += [to_cell(arena.tile(t), None) for t in EXTRAS]
    tiles += [None] * (ICE_FIRST - len(tiles))
    tiles += [im and icy(im) for im in tiles[:EXTRA_FIRST]]
    rows = (len(tiles) + BLOB_COLS - 1) // BLOB_COLS
    out = Image.new("RGBA", (BLOB_COLS * CELL_W, rows * CELL_H))
    for i, im in enumerate(tiles):
        if im:
            out.paste(im, (i % BLOB_COLS * CELL_W, i // BLOB_COLS * CELL_H))
    return out


def fill_count(name):
    return len(TILESETS[name][2].get("fills", []))


def tile_index(cell, x, y, fills=0, surface=0):
    """Sheet index for cell (x, y); cell(x, y) -> CELL_* value (1 solid,
    2 one-way, 3 hazard, 4 ladder), outside the level counted solid. None
    for empty cells. fills: fill_count() of the sheet. surface: the
    cell's SURFACE_* (ice: the ice copy of its tile)."""
    i = terrain_index(cell, x, y, fills)
    return i + ICE_FIRST if surface == SURFACE_ICE and i is not None and i < EXTRA_FIRST else i


def terrain_index(cell, x, y, fills):
    """tile_index without the surface. A fully solid cell is plain fill 2
    times in fills + 2, else a variant (position hash)."""
    c = cell(x, y)
    if c == 1:
        m = blob_mask(lambda x, y: cell(x, y) == 1, x, y)
        h = ((x * 73856093) ^ (y * 19349663)) >> 4
        v = h % (fills + 2) - 2
        if m == 0xff and v >= 0:
            return FILL_FIRST + v
        return blob_masks().index(m)
    if c == 2:
        lf, rt = cell(x - 1, y) == 2, cell(x + 1, y) == 2
        return ONEWAY_MID if lf and rt else ONEWAY_LEFT if rt else ONEWAY_RIGHT if lf else ONEWAY_SINGLE
    if c == 3:
        return SPIKES
    if c == 4:
        return LADDER_TOP if cell(x, y - 1) != 4 else LADDER_BOTTOM if cell(x, y + 1) != 4 else LADDER_MID
    return None


def rect_tiles():
    """Sheet index of a solid cell in a solid rectangle (a moving
    platform, platform/common/scene.c), by which sides the rectangle goes
    on: index bit 0 up, 1 right, 2 down, 3 left."""
    out = []
    for i in range(16):
        solid = lambda x, y: (x, y) == (0, 0) or (
            (y >= 0 or i & 1) and (x <= 0 or i & 2) and (y <= 0 or i & 4) and (x >= 0 or i & 8))
        out.append(blob_masks().index(blob_mask(solid, 0, 0)))
    return out


def header():
    names = ", ".join(f'"tiles_{n}"' for n in TILESETS)
    rect = ", ".join(str(t) for t in rect_tiles())
    return f"""/* Generated by tools/build_sprites.py from tools/terrain.py: terrain
 * sheets, in level `tileset` id order (core/level.h). */
#ifndef TILESETS_H
#define TILESETS_H

#define TILESET_COUNT {len(TILESETS)}
#define TILESET_COLS {BLOB_COLS}         /* cells per sheet row */

static const char *const tileset_sheets[TILESET_COUNT] = {{{names}}};

/* Tile of a solid cell in a solid rectangle (moving platforms), by which
 * sides the rectangle goes on: bit 0 up, 1 right, 2 down, 3 left. */
static const unsigned char tileset_rect_tiles[16] = {{{rect}}};

#endif
"""


def preview(out_dir):
    lv = open(os.path.join(ROOT, "build", "assets", "levels", "arena.lvl"), "rb").read()
    cw, ch = struct.unpack(">HH", lv[4:8])
    cells = lv[16:16 + cw * ch]
    cell = lambda x, y: cells[y * cw + x] & 15 if 0 <= x < cw and 0 <= y < ch else 1
    n64 = os.path.join(ROOT, "build", "assets", "n64")
    for name, backdrop in [("arena", "forest"), ("rocky_forest", "forest"),
                           ("rocky_desert", "desert"), ("rocky_snow", "sky"),
                           ("rocky_lava", "moon")]:
        sh = sheet(name)
        sh.save(os.path.join(out_dir, f"terrain_{name}.png"))
        frame = Image.open(os.path.join(n64, f"sky_{backdrop}.png")).convert("RGBA")
        frame = frame.crop((0, 0, 640, 240))
        frame.alpha_composite(Image.open(os.path.join(n64, f"scenery_{backdrop}.png")).convert("RGBA"))
        for y in range(ch):
            for x in range(cw):
                i = tile_index(cell, x, y, fill_count(name), cells[y * cw + x] >> 4)
                if i is not None:
                    c, r = i % BLOB_COLS * CELL_W, i // BLOB_COLS * CELL_H
                    frame.alpha_composite(sh.crop((c, r, c + CELL_W, r + CELL_H)),
                                          (x * CELL_W, y * CELL_H))
        frame.resize((640, 480), Image.NEAREST).save(os.path.join(out_dir, f"arena_{name}.png"))


if __name__ == "__main__":
    preview(sys.argv[1])
