#!/usr/bin/env python3
"""Convert the levels of an LDtk project to binary arenas (format:
core/level.h).

usage: ldtk_import.py <project.ldtk> <out_dir>

Writes <out_dir>/<level identifier lowercased>.lvl per level and appends
each file name to <out_dir>/levels.txt if it is not listed yet.

LDtk works in square art px on an 8 px grid. The game draws each art px 2
sim px wide (640x240, core/level.h), so entity x and width are doubled;
cells, tiles and y are unchanged.

The level's one tile layer is made from the Collision grid with the
level's tileset (tools/terrain.py tile_index: corners, edges and fill
from each cell's neighbors). LDtk's own tile layers are for the editor
view only and are not read.

Level rules (errors stop the import):
  - 8 px grid; one IntGrid layer named "Collision" (values 1-4);
  - optional IntGrid layer "Surface" (1 = ice), only on solid and one-way
    cells: the high nibble of each cell (core/level.h SURFACE_*);
  - entities: see ENTITY_TYPES (at most one start and goal per team, up
    to MAX_FLAGS flags per team; goals are resizable zones);
  - optional level fields: Music (Int), Backdrop (String, a
    tools/scenery.py BACKDROPS name, default forest), Tileset (String, a
    tools/terrain.py TILESETS name, default arena).
"""
import argparse
import json
import os
import struct
import sys

import scenery
import terrain

GRID = 8
XS = 2                                  # sim px per art px, horizontally
ENTITY_TYPES = {"P1Start": 1, "P2Start": 2, "JumpPad": 3, "Flag1": 4, "Flag2": 5, "Goal1": 6,
                "Goal2": 7, "FruitSpot": 8}
UNIQUE = {"P1Start", "P2Start", "Goal1", "Goal2"}
MAX_FLAGS = 3                           # per team (core/game.h FLAGS_PER_TEAM)
TILE_EMPTY = 0xFFFF
FLAG_NONE = 0xFFFF
BACKDROPS = [b[0] for b in scenery.BACKDROPS]
TILESETS = list(terrain.TILESETS)


def fields(inst):
    return {f["__identifier"]: f["__value"] for f in inst.get("fieldInstances", [])}


def convert(level):
    name = level["identifier"]
    errors = []
    layers = level["layerInstances"] or []
    coll = [li for li in layers if li["__identifier"] == "Collision" and li["__type"] == "IntGrid"]
    if len(coll) != 1:
        raise SystemExit(f"{name}: need exactly one IntGrid layer 'Collision'")
    coll = coll[0]
    w, h = coll["__cWid"], coll["__cHei"]
    cells = coll["intGridCsv"]
    if coll["__gridSize"] != GRID:
        errors.append(f"Collision grid {coll['__gridSize']} px, expected {GRID}")
    if any(v not in (0, 1, 2, 3, 4) for v in cells):
        errors.append("Collision values must be 0-4")
    surf = [li for li in layers if li["__identifier"] == "Surface" and li["__type"] == "IntGrid"]
    surface = surf[0]["intGridCsv"] if surf else [0] * len(cells)
    if len(surface) != len(cells):
        errors.append("Surface layer must match the Collision grid")
        surface = [0] * len(cells)
    for i, (c, s) in enumerate(zip(cells, surface)):
        if s not in (0, terrain.SURFACE_ICE):
            errors.append(f"Surface value {s} at cell {i % w},{i // w}: only 1 (ice)")
        elif s and c not in (1, 2):
            errors.append(f"Surface at cell {i % w},{i // w}: not on a solid or one-way cell")

    lf = fields(level)
    backdrop, tileset = lf.get("Backdrop") or "forest", lf.get("Tileset") or "arena"
    if backdrop not in BACKDROPS:
        errors.append(f"Backdrop {backdrop!r}: one of {', '.join(BACKDROPS)}")
    if tileset not in TILESETS:
        errors.append(f"Tileset {tileset!r}: one of {', '.join(TILESETS)}")
    cell = lambda x, y: cells[y * w + x] if 0 <= x < w and 0 <= y < h else 1
    fills = terrain.fill_count(tileset) if tileset in TILESETS else 0
    grid = [terrain.tile_index(cell, x, y, fills, surface[y * w + x])
            for y in range(h) for x in range(w)]
    tile_layers = [[TILE_EMPTY if t is None else t for t in grid]]

    entities, seen = [], set()
    for li in layers:
        for e in li["entityInstances"]:
            ident = e["__identifier"]
            kind = ENTITY_TYPES.get(ident)
            if kind is None:
                errors.append(f"unknown entity {ident}")
                continue
            if ident in UNIQUE:
                if ident in seen:
                    errors.append(f"more than one {ident}")
                seen.add(ident)
            x = e["px"][0] + li["__pxTotalOffsetX"] - round(e["width"] * e["__pivot"][0])
            y = e["px"][1] + li["__pxTotalOffsetY"] - round(e["height"] * e["__pivot"][1])
            entities.append((kind, x * XS, y, e["width"] * XS, e["height"], x * XS, y, 0, FLAG_NONE))
    for ident in ("Flag1", "Flag2"):
        n = sum(1 for e in entities if e[0] == ENTITY_TYPES[ident])
        if n > MAX_FLAGS:
            errors.append(f"{n} {ident} entities, at most {MAX_FLAGS}")
    entities.sort(key=lambda e: (e[0], e[2], e[1]))  # stable order for the core

    if errors:
        for e in errors:
            print(f"{name}: {e}", file=sys.stderr)
        raise SystemExit(1)

    out = bytearray(b"LVL3")
    out += struct.pack(">HHHH", w, h, len(tile_layers), len(entities))
    out += struct.pack(">BBBB", 0, lf.get("Music") or 0, BACKDROPS.index(backdrop),
                       TILESETS.index(tileset))
    out += bytes(c | s << 4 for c, s in zip(cells, surface))
    if len(out) & 1:
        out += b"\0"
    for grid in tile_layers:
        out += struct.pack(">hh", 0, 0)
        out += struct.pack(f">{len(grid)}H", *grid)
    for rec in entities:
        out += struct.pack(">HhhHHhhhH", *rec)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("out_dir")
    args = ap.parse_args()
    with open(args.project) as f:
        project = json.load(f)
    os.makedirs(args.out_dir, exist_ok=True)
    listing = os.path.join(args.out_dir, "levels.txt")
    names = open(listing).read().split() if os.path.exists(listing) else []
    for level in project["levels"]:
        data = convert(level)
        fname = level["identifier"].lower() + ".lvl"
        with open(os.path.join(args.out_dir, fname), "wb") as f:
            f.write(data)
        print(f"wrote {os.path.join(args.out_dir, fname)} ({len(data)} bytes)")
        if fname not in names:
            names.append(fname)
            with open(listing, "a") as f:
                f.write(fname + "\n")


if __name__ == "__main__":
    main()
