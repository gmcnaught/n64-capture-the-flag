#!/usr/bin/env python3
"""Convert the levels of an LDtk project to binary arenas (format:
core/level.h).

usage: ldtk_import.py <project.ldtk> <out_dir>

Writes <out_dir>/<level identifier lowercased>.lvl per level and appends
each file name to <out_dir>/levels.txt if it is not listed yet.

LDtk works in square art px on an 8 px grid. The game draws each art px 2
sim px wide (640x240, core/level.h), so entity x and width are doubled;
cells, tiles and y are unchanged.

Level rules (errors stop the import):
  - 8 px grid; one IntGrid layer named "Collision" (values 1-4);
  - Tiles/AutoLayer layers all use one tileset; non-empty ones are stored
    bottom first. AutoLayer tiles are read from the baked autoLayerTiles
    that LDtk saves;
  - entities: see ENTITY_TYPES (at most one start and goal per team, up
    to MAX_FLAGS flags per team; goals are resizable zones);
  - optional level fields: Music (Int), Backdrop (Int).
"""
import argparse
import json
import os
import struct
import sys

GRID = 8
XS = 2                                  # sim px per art px, horizontally
ENTITY_TYPES = {"P1Start": 1, "P2Start": 2, "JumpPad": 3, "Flag1": 4, "Flag2": 5, "Goal1": 6,
                "Goal2": 7, "FruitSpot": 8}
UNIQUE = {"P1Start", "P2Start", "Goal1", "Goal2"}
MAX_FLAGS = 3                           # per team (core/game.h FLAGS_PER_TEAM)
TILE_EMPTY = 0xFFFF
FLAG_NONE = 0xFFFF
FLIP_X, FLIP_Y = 0x4000, 0x8000


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

    tile_layers = []
    tileset = None
    for li in reversed(layers):                     # LDtk lists top first
        tiles = li["gridTiles"] + li["autoLayerTiles"]
        if li["__type"] not in ("Tiles", "AutoLayer", "IntGrid") or li["__tilesetDefUid"] is None:
            continue
        if not tiles:                               # empty layer: nothing to draw
            continue
        if li["__gridSize"] != GRID or li["__cWid"] != w or li["__cHei"] != h:
            errors.append(f"layer {li['__identifier']}: must match the Collision grid")
            continue
        if tileset not in (None, li["__tilesetDefUid"]):
            errors.append(f"layer {li['__identifier']}: all tile layers must use one tileset")
        tileset = li["__tilesetDefUid"]
        grid = [TILE_EMPTY] * (w * h)
        ox, oy = li["__pxTotalOffsetX"], li["__pxTotalOffsetY"]
        for t in tiles:
            x, y = (t["px"][0] + ox) // GRID, (t["px"][1] + oy) // GRID
            if not (0 <= x < w and 0 <= y < h):
                continue
            if t["t"] > 0x0FFF:
                errors.append(f"layer {li['__identifier']}: tile id {t['t']} > 4095")
                continue
            grid[y * w + x] = t["t"] | (FLIP_X if t["f"] & 1 else 0) | (FLIP_Y if t["f"] & 2 else 0)
        tile_layers.append(grid)

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

    lf = fields(level)
    out = bytearray(b"LVL2")
    out += struct.pack(">HHHH", w, h, len(tile_layers), len(entities))
    out += struct.pack(">BBBB", 0, lf.get("Music") or 0, lf.get("Backdrop") or 0, 0)
    out += bytes(cells)
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
