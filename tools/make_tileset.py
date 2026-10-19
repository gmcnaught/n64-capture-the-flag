#!/usr/bin/env python3
"""Compose assets/src/tilesets/arena.png, the arena tileset (8x8 cells,
17 columns), from new-assets/:

usage: make_tileset.py [-o PATH]

  rows 0-11  TinyAdventurePlatformForest/Tileset-Forest-Ground-8x8.png as is
             (tools/make_arena.py names the cells it uses: 3x3 block,
             inner corners, single block, strip, pillar, thin platforms)
  row 12     0 ladder top, 1 ladder middle, 2 ladder bottom, 3 spikes:
             Rocky Roads Objects/ladder.png and Enemies/spikes.png cells,
             halved (nearest); the Tiny Adventure pack has none, and the
             arena uses neither yet
"""
import argparse
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "new-assets")
FOREST = os.path.join(ASSETS, "TinyAdventurePlatformForest", "Tileset-Forest-Ground-8x8.png")
ROCKY = os.path.join(ASSETS, "RockyRoads", "Rocky Roads")
OUT = os.path.join(ROOT, "assets", "src", "tilesets", "arena.png")
G = 8


def half_cell(path, c, r):
    """16x16 cell (c, r) of a Rocky Roads sheet, halved to 8x8."""
    im = Image.open(path).convert("RGBA")
    return im.crop((c * 16, r * 16, c * 16 + 16, r * 16 + 16)).resize((G, G), Image.NEAREST)


def build():
    forest = Image.open(FOREST).convert("RGBA")
    out = Image.new("RGBA", (forest.width, 13 * G))
    out.paste(forest, (0, 0))
    ladder = os.path.join(ROCKY, "Objects", "ladder.png")
    extras = [half_cell(ladder, 0, 1), half_cell(ladder, 0, 2), half_cell(ladder, 0, 3),
              half_cell(os.path.join(ROCKY, "Enemies", "spikes.png"), 0, 0)]
    for i, im in enumerate(extras):
        out.paste(im, (i * G, 12 * G))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=OUT)
    args = ap.parse_args()
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    build().save(args.output)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
