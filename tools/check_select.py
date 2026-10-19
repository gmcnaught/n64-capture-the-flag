#!/usr/bin/env python3
"""Pick the replays `make check` runs on the N64 (ares), from what changed.

usage: check_select.py [--base REF] [--full] [--only "NAME ..."]

Prints the replay file names (tests/replays) on one line; why each was
picked goes to stderr. The host runs every replay in `make test` (about 2
s), so the N64 run is about the N64 build: that it computes the same as
the host, and frame time. Changes are the working tree (with untracked
files) against REF (default HEAD); with none, the last commit (HEAD~1 to
HEAD).

  full suite   any change under FULL_PATHS: code every replay runs through
               the same way (fixed point, bodies, level data, movement,
               replay playback, the N64 platform and build), or --full
  otherwise    SMOKE (one short replay per stage and mechanic, and menu:
               the screens have the worst N64 frame), plus
               - replays that are new or changed,
               - replays on a level whose imported .lvl changed (LDtk
                 project or importer; levels built from REF and from the
                 working tree are compared),
               - replays for what a change touches in FEATURE_FILES (the
                 functions in its diff hunk headers and its changed lines,
                 matched against FEATURES; an extra match only adds replays)
--only NAMES runs just those (names with or without .rpl).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPLAYS = os.path.join(ROOT, "tests", "replays")
LEVELS = os.path.join(ROOT, "build", "assets", "levels")

FULL_PATHS = ["core/fixed.h", "core/body.", "core/level.", "core/player.", "core/player_tuning.h",
              "core/input.h", "platform/common/replay.", "platform/n64/", "Makefile"]
SMOKE = ["menu", "steal_drop", "duel", "stack", "ice_slide", "wind_gust", "platform_push"]
LEVEL_SOURCES = ["assets/src/levels/", "tools/ldtk_import.py", "tools/terrain.py",
                 "tools/scenery.py"]
FEATURE_FILES = ["core/game.c", "core/game.h", "platform/common/scene.c"]
# Function-name pattern in a hunk header -> replays.
FEATURES = [
    (r"water|fruit", ["water"]),
    (r"wind|zone|gust|dust|streak", ["wind_gust", "wind_drop", "desert_capture"]),
    (r"platform|solid|squish|on_top|path_step", ["platform_ride", "platform_squish",
                                                 "platform_push", "sky_capture"]),
    (r"flag|goal|carr|take|send_home", ["steal_drop", "capture", "capture_blue"]),
    (r"stand|rider|stack|collide", ["stack"]),
    (r"attack|slash|damage|combat", ["duel"]),
    (r"menu|pause|title|controls|countdown|winner|round", ["menu", "capture"]),
]


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout


def changes(base):
    """(base used, changed paths): the working tree against base, else the last commit."""
    paths = set(git("diff", "--name-only", base).split())
    paths |= set(git("ls-files", "--others", "--exclude-standard").split())
    if paths:
        return base, paths
    return "HEAD~1", set(git("diff", "--name-only", "HEAD~1", "HEAD").split())


def level_of(name):
    with open(os.path.join(REPLAYS, name + ".rpl")) as f:
        m = re.search(r"^level (\S+)", f.read(), re.M)
    return m.group(1) if m else None


def changed_levels(base):
    """Level files whose import differs between base and the working tree
    (build/assets/levels, from `make assets`); None if base cannot be built."""
    with tempfile.TemporaryDirectory() as tmp:
        tar = subprocess.run(["git", "archive", base, "tools", "assets/src/levels"], cwd=ROOT,
                             capture_output=True, check=True).stdout
        subprocess.run(["tar", "-x", "-C", tmp], input=tar, check=True)
        out = os.path.join(tmp, "out")
        for ldtk in sorted(os.listdir(os.path.join(tmp, "assets", "src", "levels"))):
            if ldtk.endswith(".ldtk"):
                r = subprocess.run([sys.executable, os.path.join(tmp, "tools", "ldtk_import.py"),
                                    os.path.join(tmp, "assets", "src", "levels", ldtk), out],
                                   capture_output=True)
                if r.returncode:
                    return None
        old = {n: open(os.path.join(out, n), "rb").read() for n in os.listdir(out)
               if n.endswith(".lvl")}
    new = {n: open(os.path.join(LEVELS, n), "rb").read() for n in os.listdir(LEVELS)
           if n.endswith(".lvl")}
    return {n for n in new if old.get(n) != new[n]}


def hunk_functions(base, path):
    """Function context of each changed hunk (git's hunk headers) and the changed lines."""
    if not os.path.exists(os.path.join(ROOT, path)):
        return []
    diff = git("diff", "-U0", base, "--", path)
    return re.findall(r"^@@ [^@]* @@ ?(.*)$", diff, re.M) + \
        [l[1:] for l in diff.splitlines() if l[:1] in "+-" and l[:3] not in ("+++", "---")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="HEAD")
    ap.add_argument("--full", action="store_true")
    ap.add_argument("--only")
    args = ap.parse_args()
    every = sorted(n[:-4] for n in os.listdir(REPLAYS) if n.endswith(".rpl"))
    picked = {}                         # name -> reason

    def pick(names, why):
        for n in names:
            if n in every:
                picked.setdefault(n, why)

    if args.only:
        pick([n[:-4] if n.endswith(".rpl") else n for n in args.only.split()], "named")
        missing = [n for n in args.only.split() if n.removesuffix(".rpl") not in every]
        if missing:
            sys.exit(f"no such replay: {' '.join(missing)}")
    else:
        base, paths = changes(args.base)
        full = [p for p in sorted(paths) if any(p.startswith(f) for f in FULL_PATHS)]
        if args.full or full:
            print("check: full suite: " + ("--full" if args.full else ", ".join(full)),
                  file=sys.stderr)
            pick(every, "full suite")
        else:
            print(f"check: changes against {base}: {len(paths)} files", file=sys.stderr)
            pick(SMOKE, "smoke")
            pick([os.path.basename(p)[:-4] for p in paths
                  if p.startswith("tests/replays/") and p.endswith(".rpl")], "new or changed")
            if any(p.startswith(s) for p in paths for s in LEVEL_SOURCES):
                levels = changed_levels(base)
                if levels is None:
                    print(f"check: levels at {base} do not build; all levels count as changed",
                          file=sys.stderr)
                    levels = {level_of(n) for n in every}
                pick([n for n in every if level_of(n) in levels],
                     "level changed: " + ", ".join(sorted(levels)))
            for path in FEATURE_FILES:
                text = "\n".join(hunk_functions(base, path)) if path in paths else ""
                for pattern, names in FEATURES:
                    if re.search(pattern, text):
                        pick(names, f"{path}: {pattern.split('|')[0]}")
    for n in sorted(picked):
        if picked[n] != "full suite":
            print(f"check: {n:20} {picked[n]}", file=sys.stderr)
    print(f"check: {len(picked)} of {len(every)} replays", file=sys.stderr)
    print(" ".join(n + ".rpl" for n in sorted(picked)))


if __name__ == "__main__":
    main()
