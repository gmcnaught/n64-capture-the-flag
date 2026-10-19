#!/usr/bin/env python3
"""Generate assets/src/levels/arena.ldtk: the capture-the-flag arena.

usage: make_arena.py [--force] [-o PATH]

The level is an ASCII map below (one character per 8x8 art cell; the game
draws each cell 16x8 sim px, core/level.h). The script writes an LDtk 1.5.3
project with:
  - Entities    P1Start, P2Start, Flag1, Flag2, JumpPad
  - Collision   IntGrid layer  (1 Solid, 2 OneWay, 3 Hazard, 4 Ladder)
  - Tiles       Tiles layer for hand-placed decoration (empty)
  - AutoTiles   AutoLayer on Collision: rules pick arena.png tiles
                (tools/make_tileset.py), so editing Collision in LDtk
                updates the visuals
and a level field Music (Int, platform/common/audio.h music id; 1 = the
game track) set to MUSIC on every level.
The AutoTiles result is also baked into the file (LDtk does the same on
save), computed by the same rules and checked against pick_tile().

Refuses to overwrite an existing file without --force, so edits made in LDtk
are not lost by re-running it.
"""
import argparse
import json
import os
import sys
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "src", "levels", "arena.ldtk")
TILESET_REL = "../tilesets/arena.png"
TILESET_PX = (136, 104)
GRID = 8
MUSIC = 1               # level music id: the game track (audio_music_names)

# Map legend:
#   #  solid      =  one-way platform   ^  hazard (spikes)   H  ladder
#   .  empty      1  P1 start   2  P2 start   J  jump pad
#   f  P1 (red) flag at home    g  P2 (blue) flag at home    (3 each)
#   r  P1 goal zone             b  P2 goal zone              (empty cells)
#   w  water fruit spot (core/game.h): only on the top slab, where the
#      stream has platforms to run down
# Entity letters mark the cell the entity stands in; a goal zone becomes
# one Goal entity covering the bounding box of its letters.

# 40 x 30 cells = 320 x 240 art px = the whole 640x240 screen (8x8 art
# cells), from a whiteboard sketch, mirrored. Columns 0-1 and 38-39 are walls inside the
# horizontal overscan. Each team's flags sit by its start on the ground;
# its goal is the base at the top on its own side, a two-row ledge out of
# the side wall with a one-row lip. A two-cell pillar from the ground up to
# the solid middle landing splits the lower half (no wall-jumping through),
# so crossing goes over the landing or the top slab. The top slab (row 9)
# ends 72 px short of each goal lip: a carrier (slower, no dash) gets over
# with a double jump. The other slabs are
# one-way, in tiers reached by a jump (34 px) or a double jump (66 px).
ARENA = """\
##....................................##
##....................................##
##....................................##
##....................................##
##rrrrrrr......................bbbbbbb##
##rrrrrrr#....................#bbbbbbb##
##########....................##########
##########....................##########
##................w..w................##
##................====................##
##....................................##
##....................................##
##....................................##
##....................................##
##========......########......========##
##.................##.................##
##.................##.................##
##.................##.................##
##.................##.................##
##.................##.................##
##..======.........##.........======..##
##.................##.................##
##.................##.................##
##.................##.................##
##........========.##.========........##
##.................##.................##
##.1.f.f.f..###....##....###..g.g.g.2.##
########################################
########################################
########################################
"""

LEVELS = [("Arena", ARENA)]
ENTITY_CHARS = "12fgJw"
ZONE_CHARS = "rb"

# arena.png cells (col, row), see tools/make_tileset.py (Tiny Adventure
# Forest ground tileset).
SOLID_9 = {
    "tl": (1, 1), "t": (2, 1), "tr": (3, 1),
    "l": (1, 2), "c": (2, 2), "r": (3, 2),
    "bl": (1, 3), "b": (2, 3), "br": (3, 3),
}
# Fully surrounded cells whose diagonal neighbor is open (concave corners),
# checked in this order.
INNER = [((1, -1), (5, 3)), ((-1, -1), (7, 3)), ((1, 1), (5, 1)), ((-1, 1), (7, 1))]
SOLID_STRIP = {"single": (1, 5), "left": (3, 5), "mid": (4, 5), "right": (5, 5)}
SOLID_PILLAR = {"top": (9, 3), "mid": (9, 5), "bottom": (9, 1)}
ONEWAY_STRIP = {"single": (15, 1), "left": (15, 2), "mid": (15, 3), "right": (15, 4)}
SPIKES = {"floor": (3, 12), "ceiling": (3, 12)}  # no flipped variant yet
LADDER = {"top": (0, 12), "mid": (1, 12), "bottom": (2, 12)}
# Entity editor icons (col, row) and colors.
ENTITIES = [  # (char, identifier, icon, color, max per level)
    ("1", "P1Start", (4, 1), "#FF5FA8", 1),
    ("2", "P2Start", (4, 1), "#3FA8FF", 1),
    ("f", "Flag1", (8, 1), "#E03C5A", 3),
    ("g", "Flag2", (8, 1), "#3C78E0", 3),
    ("J", "JumpPad", (3, 6), "#F7A41D", 0),
    ("w", "FruitSpot", (7, 5), "#3FA8FF", 0),
    ("r", "Goal1", (4, 3), "#E03C5A", 1),     # resizable zones
    ("b", "Goal2", (4, 3), "#3C78E0", 1),
]


def parse_layout(text):
    rows = text.splitlines()
    w, h = len(rows[0]), len(rows)
    assert all(len(r) == w for r in rows), "ragged layout"
    grid, marks = [], {}
    for y, row in enumerate(rows):
        line = []
        for x, ch in enumerate(row):
            if ch in ENTITY_CHARS or ch in ZONE_CHARS:
                marks.setdefault(ch, []).append((x, y))
                ch = "."
            line.append({"#": 1, "=": 2, "^": 3, "H": 4, ".": 0}[ch])
        grid.append(line)
    return w, h, grid, marks


def pick_tile(grid, x, y, oob_solid=True):
    w, h = len(grid[0]), len(grid)
    v = grid[y][x]

    def same(nx, ny):  # out of bounds counts as solid so borders look continuous
        if nx < 0 or ny < 0 or nx >= w or ny >= h:
            return v == 1 and oob_solid
        return grid[ny][nx] == v

    up, dn, lf, rt = same(x, y - 1), same(x, y + 1), same(x - 1, y), same(x + 1, y)
    if v == 3:
        above_solid = y > 0 and grid[y - 1][x] == 1
        return SPIKES["ceiling"] if above_solid and not (y + 1 < h and grid[y + 1][x] == 1) else SPIKES["floor"]
    if v == 4:
        return LADDER["top"] if not up else LADDER["bottom"] if not dn else LADDER["mid"]
    if v == 2:
        strip = ONEWAY_STRIP
    elif not up and not dn:
        strip = SOLID_STRIP
    else:
        strip = None
    if strip:
        if not lf and not rt:
            return strip["single"]
        return strip["left"] if not lf else strip["right"] if not rt else strip["mid"]
    if not lf and not rt:
        return SOLID_PILLAR["top"] if not up else SOLID_PILLAR["bottom"] if not dn else SOLID_PILLAR["mid"]
    key = ("t" if not up else "b" if not dn else "") + ("l" if not lf else "r" if not rt else "")
    if not key:
        for (dx, dy), tile in INNER:
            if not same(x + dx, y + dy):
                return tile
    return SOLID_9[key or "c"]


# ---- Auto-layer rules --------------------------------------------------------
# LDtk pattern values: 0 = any, v = cell must be v, -v = cell must not be v.
ANY = 0


def _pattern(center, up=ANY, dn=ANY, lf=ANY, rt=ANY):
    return [ANY, up, ANY,
            lf, center, rt,
            ANY, dn, ANY]


def rule_specs():
    """(pattern, tile (col,row), outOfBoundsValue), in priority order. Mirrors
    pick_tile(): inner corners, solids (out of bounds counts as solid),
    one-way, hazard, ladder."""
    specs = []
    for (dx, dy), tile in INNER:
        pat = [1] * 9
        pat[(dy + 1) * 3 + dx + 1] = -1
        for i in (0, 2, 6, 8):      # other diagonals: any
            if i != (dy + 1) * 3 + dx + 1:
                pat[i] = ANY
        specs.append((pat, tile, 1))
    for up in (1, -1):
        for dn in (1, -1):
            for lf in (1, -1):
                for rt in (1, -1):
                    g = [[1, 0, 1], [0, 1, 0], [1, 0, 1]]  # solid diagonals: no inner corner
                    g[0][1], g[2][1], g[1][0], g[1][2] = (int(up > 0), int(dn > 0),
                                                          int(lf > 0), int(rt > 0))
                    tile = pick_tile(g, 1, 1, oob_solid=False)
                    specs.append((_pattern(1, up, dn, lf, rt), tile, 1))
    for lf in (2, -2):
        for rt in (2, -2):
            g = [[0, 0, 0], [0 if lf < 0 else 2, 2, 0 if rt < 0 else 2], [0, 0, 0]]
            specs.append((_pattern(2, lf=lf, rt=rt), pick_tile(g, 1, 1, oob_solid=False), 0))
    specs.append((_pattern(3, up=1, dn=-1), SPIKES["ceiling"], 0))
    specs.append((_pattern(3), SPIKES["floor"], 0))
    specs.append((_pattern(4, up=-4), LADDER["top"], 0))
    specs.append((_pattern(4, dn=-4), LADDER["bottom"], 0))
    specs.append((_pattern(4), LADDER["mid"], 0))
    return specs


def rule_def(uid, pattern, tile_id, oob):
    return {
        "uid": uid, "active": True, "size": 3, "tileRectsIds": [[tile_id]],
        "alpha": 1, "chance": 1, "breakOnMatch": True, "pattern": pattern,
        "flipX": False, "flipY": False, "xModulo": 1, "yModulo": 1,
        "xOffset": 0, "yOffset": 0, "tileXOffset": 0, "tileYOffset": 0,
        "tileRandomXMin": 0, "tileRandomXMax": 0, "tileRandomYMin": 0, "tileRandomYMax": 0,
        "checker": "None", "tileMode": "Single", "pivotX": 0, "pivotY": 0,
        "outOfBoundsValue": oob, "invalidated": False,
        "perlinActive": False, "perlinSeed": 0, "perlinScale": 0.2, "perlinOctaves": 2,
    }


def rule_matches(grid, x, y, pattern, oob):
    w, h = len(grid[0]), len(grid)
    for i, p in enumerate(pattern):
        if p == ANY:
            continue
        nx, ny = x + i % 3 - 1, y + i // 3 - 1
        v = grid[ny][nx] if 0 <= nx < w and 0 <= ny < h else oob
        if (p > 0 and v != p) or (p < 0 and v == -p):
            return False
    return True


def bake_auto_tiles(grid, rules, tcw):
    """Apply rules like LDtk: per cell, the first matching rule wins."""
    w, h = len(grid[0]), len(grid)
    out = []
    for y in range(h):
        for x in range(w):
            for r in rules:
                if rule_matches(grid, x, y, r["pattern"], r["outOfBoundsValue"]):
                    t = r["tileRectsIds"][0][0]
                    c, row = t % tcw, t // tcw
                    assert (c, row) == pick_tile(grid, x, y), (x, y)
                    out.append({"px": [x * GRID, y * GRID], "src": [c * GRID, row * GRID],
                                "f": 0, "t": t, "d": [r["uid"], y * w + x], "a": 1})
                    break
    return out


def iid():
    return str(uuid.uuid4())


def layer_def(type_, ident, uid, **extra):
    d = {
        "__type": type_, "identifier": ident, "type": type_, "uid": uid, "doc": None,
        "uiColor": None, "gridSize": GRID, "guideGridWid": 0, "guideGridHei": 0,
        "displayOpacity": 1, "inactiveOpacity": 1, "hideInList": False,
        "hideFieldsWhenInactive": True, "canSelectWhenInactive": True,
        "renderInWorldView": True, "pxOffsetX": 0, "pxOffsetY": 0,
        "parallaxFactorX": 0, "parallaxFactorY": 0, "parallaxScaling": False,
        "requiredTags": [], "excludedTags": [], "autoTilesKilledByOtherLayerUid": None,
        "uiFilterTags": [], "useAsyncRender": False, "intGridValues": [],
        "intGridValuesGroups": [], "autoRuleGroups": [], "autoSourceLayerDefUid": None,
        "tilesetDefUid": None, "tilePivotX": 0, "tilePivotY": 0, "biomeFieldUid": None,
    }
    d.update(extra)
    return d


def entity_def(ident, uid, color, icon, tileset_uid, max_count=1, resizable=False):
    return {
        "identifier": ident, "uid": uid, "tags": [], "exportToToc": False,
        "allowOutOfBounds": False, "doc": None, "width": GRID, "height": GRID,
        "resizableX": resizable, "resizableY": resizable, "minWidth": None, "maxWidth": None,
        "minHeight": None, "maxHeight": None, "keepAspectRatio": False,
        "tileOpacity": 1, "fillOpacity": 0.08, "lineOpacity": 0, "hollow": False,
        "color": color, "renderMode": "Tile", "showName": True,
        "tilesetId": tileset_uid, "tileRenderMode": "FitInside",
        "tileRect": {"tilesetUid": tileset_uid, "x": icon[0] * GRID, "y": icon[1] * GRID,
                     "w": GRID, "h": GRID},
        "uiTileRect": None, "nineSliceBorders": [], "maxCount": max_count,
        "limitScope": "PerLevel", "limitBehavior": "MoveLastOne",
        "pivotX": 0.5, "pivotY": 1, "fieldDefs": [],
    }


def layer_inst(ldef, cw, ch, level_uid, seed, tileset_uid=None, **content):
    d = {
        "__identifier": ldef["identifier"], "__type": ldef["__type"],
        "__cWid": cw, "__cHei": ch, "__gridSize": GRID, "__opacity": 1,
        "__pxTotalOffsetX": 0, "__pxTotalOffsetY": 0,
        "__tilesetDefUid": tileset_uid,
        "__tilesetRelPath": TILESET_REL if tileset_uid is not None else None,
        "iid": iid(), "levelId": level_uid, "layerDefUid": ldef["uid"],
        "pxOffsetX": 0, "pxOffsetY": 0, "visible": True, "optionalRules": [],
        "intGridCsv": [], "autoLayerTiles": [], "seed": seed,
        "overrideTilesetUid": None, "gridTiles": [], "entityInstances": [],
    }
    d.update(content)
    return d


def entity_inst(edef, cell, color, size=(1, 1)):
    """size: cells; cell is the top-left one."""
    x, y = cell
    w, h = size[0] * GRID, size[1] * GRID
    px = [x * GRID + w // 2, y * GRID + h]  # pivot (0.5, 1): bottom-center
    return {
        "__identifier": edef["identifier"], "__grid": [x, y], "__pivot": [0.5, 1],
        "__tags": [], "__tile": dict(edef["tileRect"]), "__smartColor": color,
        "__worldX": px[0], "__worldY": px[1],
        "iid": iid(), "width": w, "height": h, "defUid": edef["uid"],
        "px": px, "fieldInstances": [],
    }


def build():
    tcw = TILESET_PX[0] // GRID
    tileset_uid, coll_uid, tiles_uid, ents_uid = 1, 2, 3, 4
    auto_uid, group_uid = 9, 10
    rules = [rule_def(11 + i, pat, r * tcw + c, oob)
             for i, (pat, (c, r), oob) in enumerate(rule_specs())]
    next_uid = 11 + len(rules)

    coll_def = layer_def(
        "IntGrid", "Collision", coll_uid, displayOpacity=0.35, uiColor="#FFFFFF",
        intGridValues=[
            {"value": 1, "identifier": "Solid", "color": "#E8F0FF", "tile": None, "groupUid": 0},
            {"value": 2, "identifier": "OneWay", "color": "#9FB4D8", "tile": None, "groupUid": 0},
            {"value": 3, "identifier": "Hazard", "color": "#E03C3C", "tile": None, "groupUid": 0},
            {"value": 4, "identifier": "Ladder", "color": "#FF9A1F", "tile": None, "groupUid": 0},
        ])
    tiles_def = layer_def("Tiles", "Tiles", tiles_uid, tilesetDefUid=tileset_uid)
    auto_def = layer_def(
        "AutoLayer", "AutoTiles", auto_uid, tilesetDefUid=tileset_uid,
        autoSourceLayerDefUid=coll_uid,
        autoRuleGroups=[{
            "uid": group_uid, "name": "Collision tiles", "active": True, "isOptional": False,
            "usesWizard": False, "biomeRequirementMode": 0, "requiredBiomeValues": [],
            "color": None, "icon": None, "rules": rules,
        }])
    ents_def = layer_def("Entities", "Entities", ents_uid, uiColor="#0099DB",
                         hideFieldsWhenInactive=False)
    edefs = {}
    for i, (ch, ident, icon, color, max_count) in enumerate(ENTITIES):
        edefs[ch] = (entity_def(ident, next_uid, color, icon, tileset_uid, max_count,
                                resizable=ch in ZONE_CHARS), color)
        next_uid += 1

    music_uid = next_uid
    next_uid += 1
    music_def = {
        "identifier": "Music", "doc": "Music id (platform/common/audio.h), 0 = silence",
        "__type": "Int", "uid": music_uid, "type": "F_Int", "isArray": False,
        "canBeNull": False, "arrayMinLength": None, "arrayMaxLength": None,
        "editorDisplayMode": "Hidden", "editorDisplayScale": 1, "editorDisplayPos": "Above",
        "editorLinkStyle": "StraightArrow", "editorDisplayColor": None,
        "editorAlwaysShow": False, "editorShowInWorld": True, "editorCutLongValues": True,
        "editorTextSuffix": None, "editorTextPrefix": None, "useForSmartColor": False,
        "exportToToc": False, "searchable": False, "min": 0, "max": 255, "regex": None,
        "acceptFileTypes": None, "defaultOverride": {"id": "V_Int", "params": [0]},
        "textLanguageMode": None, "symmetricalRef": False, "autoChainRef": True,
        "allowOutOfLevelRef": True, "allowedRefs": "Any", "allowedRefsEntityUid": None,
        "allowedRefTags": [], "tilesetUid": None,
    }

    levels = []
    world_x = 0
    for ident, text in LEVELS:
        w, h, grid, marks = parse_layout(text)
        level_uid = next_uid
        next_uid += 1
        auto_tiles = bake_auto_tiles(grid, rules, tcw)
        ents = [entity_inst(edefs[k][0], cell, edefs[k][1])
                for k in ENTITY_CHARS for cell in marks.get(k, [])]
        for k in ZONE_CHARS:
            cells = marks.get(k, [])
            if cells:
                x0, y0 = min(c for c, _ in cells), min(r for _, r in cells)
                x1, y1 = max(c for c, _ in cells), max(r for _, r in cells)
                ents.append(entity_inst(edefs[k][0], (x0, y0), edefs[k][1],
                                        (x1 - x0 + 1, y1 - y0 + 1)))
        layers = [  # LDtk order: first = drawn on top
            layer_inst(ents_def, w, h, level_uid, 101, entityInstances=ents),
            layer_inst(coll_def, w, h, level_uid, 102,
                       intGridCsv=[v for row in grid for v in row]),
            layer_inst(tiles_def, w, h, level_uid, 103, tileset_uid),
            layer_inst(auto_def, w, h, level_uid, 105, tileset_uid, autoLayerTiles=auto_tiles),
        ]
        levels.append({
            "identifier": ident, "iid": iid(), "uid": level_uid,
            "worldX": world_x, "worldY": 0, "worldDepth": 0,
            "pxWid": w * GRID, "pxHei": h * GRID,
            "__bgColor": "#8CCEEF", "bgColor": "#8CCEEF", "useAutoIdentifier": False,
            "bgRelPath": None, "bgPos": None, "bgPivotX": 0.5, "bgPivotY": 0.5,
            "__smartColor": "#8C8A93", "__bgPos": None, "externalRelPath": None,
            "fieldInstances": [{
                "__identifier": "Music", "__type": "Int", "__value": MUSIC, "__tile": None,
                "defUid": music_uid, "realEditorValues": [{"id": "V_Int", "params": [MUSIC]}],
            }],
            "layerInstances": layers, "__neighbours": [],
        })
        world_x += w * GRID + 64

    return {
        "__header__": {
            "fileType": "LDtk Project JSON", "app": "LDtk", "doc": "https://ldtk.io/json",
            "schema": "https://ldtk.io/files/JSON_SCHEMA.json",
            "appAuthor": "Sebastien 'deepnight' Benard", "appVersion": "1.5.3",
            "url": "https://ldtk.io",
        },
        "iid": iid(), "jsonVersion": "1.5.3", "appBuildId": 473703, "nextUid": next_uid,
        "identifierStyle": "Capitalize", "toc": [], "worldLayout": "Free",
        "worldGridWidth": 320, "worldGridHeight": 240,
        "defaultLevelWidth": 320, "defaultLevelHeight": 240,
        "defaultPivotX": 0.5, "defaultPivotY": 1, "defaultGridSize": GRID,
        "defaultEntityWidth": GRID, "defaultEntityHeight": GRID,
        "bgColor": "#2B2838", "defaultLevelBgColor": "#8CCEEF",
        "minifyJson": False, "externalLevels": False, "exportTiled": False,
        "simplifiedExport": False, "imageExportMode": "None", "exportLevelBg": True,
        "pngFilePattern": None, "backupOnSave": False, "backupLimit": 10,
        "backupRelPath": None, "levelNamePattern": "%world_Level_%idx",
        "tutorialDesc": None, "customCommands": [], "flags": [],
        "defs": {
            "layers": [ents_def, coll_def, tiles_def, auto_def],
            "entities": [e for e, _ in edefs.values()],
            "tilesets": [{
                "__cWid": tcw, "__cHei": TILESET_PX[1] // GRID,
                "identifier": "Arena", "uid": tileset_uid, "relPath": TILESET_REL,
                "embedAtlas": None, "pxWid": TILESET_PX[0], "pxHei": TILESET_PX[1],
                "tileGridSize": GRID, "spacing": 0, "padding": 0, "tags": [],
                "tagsSourceEnumUid": None, "enumTags": [], "customData": [],
                "savedSelections": [], "cachedPixelData": None,
            }],
            "enums": [], "externalEnums": [], "levelFields": [music_def],
        },
        "levels": levels, "worlds": [], "dummyWorldIid": iid(),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=OUT)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    if os.path.exists(args.output) and not args.force:
        sys.exit(f"{args.output} exists; pass --force to overwrite (LDtk edits will be lost)")
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(build(), f, indent=1)
        f.write("\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
