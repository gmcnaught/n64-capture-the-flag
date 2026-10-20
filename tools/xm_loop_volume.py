#!/usr/bin/env python3
"""Make an XM module start at full global volume, so a song that fades
out at its end is heard again when it loops.

usage: xm_loop_volume.py FILE.xm ...

libxm (N64 libdragon and host) sets the global volume to full only when it
loads a module: a song that ends on a global volume fade (Gxx/Hxx) loops,
and is sought back to the top, silent. This puts G40 (global volume 0x40)
in a free effect slot on row 0 of the song's first pattern, rewriting that
pattern in the packed form. Files that already have it are left as they
are.
"""

import struct
import sys

G_SET = 0x10                # XM effect G: set global volume
FULL = 0x40


def read_cells(d, q, end, rows, nch):
    cells = []
    while q < end:
        b = d[q]
        q += 1
        if b & 0x80:
            cell = [0, 0, 0, 0, 0]
            for i in range(5):
                if b & (1 << i):
                    cell[i] = d[q]
                    q += 1
        else:
            cell = [b] + list(d[q:q + 4])
            q += 4
        cells.append(cell)
    if len(cells) != rows * nch:
        raise SystemExit(f"pattern has {len(cells)} cells, expected {rows * nch}")
    return cells


def pack(cells):
    out = bytearray()
    for cell in cells:
        mask = sum(1 << i for i in range(5) if cell[i])
        out.append(0x80 | mask)
        out += bytes(v for v in cell if v)
    return bytes(out)


def fix(path):
    d = bytearray(open(path, "rb").read())
    if d[:17] != b"Extended Module: ":
        raise SystemExit(f"{path}: not an XM module")
    hs = struct.unpack_from("<I", d, 60)[0]
    songlen, _, nch, npat = struct.unpack_from("<4H", d, 64)
    first = d[80] if songlen else 0
    off = 60 + hs
    for p in range(npat):
        phl, _, rows, psize = struct.unpack_from("<IBHH", d, off)
        start = off + phl
        if p != first:
            off = start + psize
            continue
        cells = read_cells(d, start, start + psize, rows, nch)
        row0 = cells[:nch]
        if any(c[3] == G_SET and c[4] == FULL for c in row0):
            print(f"{path}: already starts at full global volume")
            return
        free = next((c for c in row0 if c[3] == 0 and c[4] == 0), None)
        if free is None:
            raise SystemExit(f"{path}: no free effect slot on row 0 of pattern {p}")
        free[3], free[4] = G_SET, FULL
        data = pack(cells)
        struct.pack_into("<H", d, off + 7, len(data))
        d[start:start + psize] = data
        open(path, "wb").write(d)
        print(f"{path}: G40 on row 0 of pattern {p} (channel {row0.index(free) + 1})")
        return
    raise SystemExit(f"{path}: first pattern {first} not found")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for a in sys.argv[1:]:
        fix(a)
