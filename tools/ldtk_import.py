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
    to MAX_FLAGS flags per team; goals are resizable zones; up to
    MAX_WINDS Wind zones, fields Force (px/frame, as the tuning values),
    Cycle and Gust (frames, Cycle > Gust + WIND_WARN, Gust > 2 *
    WIND_RAMP), Phase (frames, < 8192), Alternate and Dust: core/level.h
    ENT_WIND); up to MAX_PLATFORMS Platform boxes, fields End (Point: the
    top-left cell at the other end), Speed (px/frame along the longer
    axis, sim px; 0-8), OneWay (bool), Phase (frames, < 32768): core/level.h
    ENT_PLATFORM. A platform's path (the box swept from its start to End)
    must not cross solid cells, and no start, flag, goal or fruit spot may
    overlap it;
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
                "Goal2": 7, "FruitSpot": 8, "Wind": 9, "Platform": 10}
UNIQUE = {"P1Start", "P2Start", "Goal1", "Goal2"}
WIND_WARN, WIND_RAMP, MAX_WINDS = 60, 20, 4     # core/game.h
MAX_PLATFORMS = 8                       # core/game.h
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

    entities, seen, paths = [], set(), []
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
            rec = (kind, x * XS, y, e["width"] * XS, e["height"], x * XS, y, 0, FLAG_NONE)
            if ident == "Wind":
                ef = fields(e)
                force, cycle, gust = round(ef["Force"] * 256), ef["Cycle"], ef["Gust"]
                if not (0 < abs(force) < 0x8000):
                    errors.append(f"Wind Force {ef['Force']}: nonzero, under 128")
                if not (2 * WIND_RAMP < gust and gust + WIND_WARN < cycle < 0x10000):
                    errors.append(f"Wind Cycle {cycle} / Gust {gust}: need Cycle > Gust + "
                                  f"{WIND_WARN}, Gust > {2 * WIND_RAMP}")
                phase = ef.get("Phase") or 0
                if not 0 <= phase < 8192:
                    errors.append(f"Wind Phase {phase}: 0-8191")
                flags = (1 if ef["Alternate"] else 0) | (2 if ef.get("Dust") else 0) | phase << 2
                rec = rec[:5] + (gust, flags, force, cycle)
            if ident == "Platform":
                ef = fields(e)
                end, phase = ef.get("End"), ef.get("Phase") or 0
                speed = round((ef.get("Speed") or 0) * 256)
                if end is None:
                    errors.append(f"Platform at cell {x // GRID},{y // GRID}: no End")
                    end = {"cx": x // GRID, "cy": y // GRID}
                if not 0 < speed < 8 * 256:
                    errors.append(f"Platform Speed {ef.get('Speed')}: above 0, under 8")
                if not 0 <= phase < 0x8000:
                    errors.append(f"Platform Phase {phase}: 0-32767")
                x2, y2 = end["cx"] * GRID, end["cy"] * GRID
                rec = rec[:5] + (x2 * XS, y2, speed, (1 if ef.get("OneWay") else 0) | phase << 1)
                swept = (min(x, x2), min(y, y2), max(x, x2) + e["width"], max(y, y2) + e["height"])
                for cy in range(swept[1] // GRID, (swept[3] + GRID - 1) // GRID):
                    for cx in range(swept[0] // GRID, (swept[2] + GRID - 1) // GRID):
                        if cell(cx, cy) == 1:
                            errors.append(f"Platform at cell {x // GRID},{y // GRID}: path "
                                          f"crosses solid cell {cx},{cy}")
                paths.append(swept)
            entities.append(rec)
    if sum(1 for e in entities if e[0] == ENTITY_TYPES["Wind"]) > MAX_WINDS:
        errors.append(f"more than {MAX_WINDS} Wind zones")
    if len(paths) > MAX_PLATFORMS:
        errors.append(f"more than {MAX_PLATFORMS} Platforms")
    for e in entities:
        if e[0] in (ENTITY_TYPES["Wind"], ENTITY_TYPES["Platform"], ENTITY_TYPES["JumpPad"]):
            continue
        ex, ey, ew, eh = e[1] // XS, e[2], e[3] // XS, e[4]
        for px0, py0, px1, py1 in paths:
            if ex < px1 and ex + ew > px0 and ey < py1 and ey + eh > py0:
                kind = next(k for k, v in ENTITY_TYPES.items() if v == e[0])
                errors.append(f"{kind} at cell {ex // GRID},{ey // GRID} on a Platform's path")
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
