#!/usr/bin/env python3
"""Build replay files by script: hold inputs until a condition on the game
state holds, using the deterministic headless host (--trace).

    from replay_builder import Builder
    b = Builder("level arena.lvl\\nabilities 7\\n")
    b.hold("RC / LC", until=lambda s: s.p[0].x >= 200)  # P1 right, P2 left
    b.hold("RJC / C", 20)                               # P1 jumps, held 20 frames
    b.hold("C / C", until=lambda s: s.p[0].grounded)
    b.save("tests/replays/example.rpl")                 # then `make replay-update`

Inputs are replay lines without the frame count (platform/common/replay.h):
P1 buttons, then optionally " / " and P2 buttons. State per frame (from
ctf --trace): frame, p[0] and p[1] with x, y, vx, vy, st (PS_*), grounded,
hp, atk (attack_timer), dead, flag (index carried or -1), wins, winner
(0, or team + 1 during the round-end banner), fruit (0/1), spot (fruit
spot index), water (stream length, 0 = none), caught (in the water),
title (title screen), paused, menu (pause menu cursor), wfx/wfy (water
front cell, -1 without water), rx (sub-pixel x remainder, Q16.16).
Conditions are checked on the state after
each frame of the new run.
"""
import os
import re
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "platform", "host", "build", "ctf")
LINE = re.compile(r"^(\d+) P(\d) x (-?\d+) y (-?\d+) vx (\S+) vy (\S+) st (\d+) gnd (\d+) "
                  r"hp (\d+) atk (\d+) dead (\d+) flag (-?\d+) wins (\d+) winner (\d+) "
                  r"fruit (\d+) spot (\d+) water (\d+) caught (\d+) "
                  r"title (\d+) paused (\d+) menu (\d+) wfx (-?\d+) wfy (-?\d+) rx (-?\d+) cd (\d+)")


class PlayerState:
    def __init__(self, g):
        self.x, self.y = int(g[2]), int(g[3])
        self.vx, self.vy = float(g[4]), float(g[5])
        self.st, self.grounded, self.hp = int(g[6]), int(g[7]) == 1, int(g[8])
        self.atk, self.dead = int(g[9]), int(g[10]) == 1
        self.flag, self.wins, self.winner = int(g[11]), int(g[12]), int(g[13])
        self.fruit, self.spot, self.water, self.caught = (int(g[14]), int(g[15]), int(g[16]),
                                                          int(g[17]) == 1)
        self.title, self.paused, self.menu = int(g[18]) == 1, int(g[19]), int(g[20])
        self.wfx, self.wfy = int(g[21]), int(g[22])
        self.rx = int(g[23])            # body.rem_x, Q16.16 sub-pixel position
        self.cd = int(g[24])            # round countdown frames left

    def __repr__(self):
        return (f"x {self.x} y {self.y} vx {self.vx} vy {self.vy} st {self.st} "
                f"gnd {int(self.grounded)} hp {self.hp} atk {self.atk} dead {int(self.dead)} "
                f"flag {self.flag} wins {self.wins} winner {self.winner} fruit {self.fruit} "
                f"spot {self.spot} water {self.water} caught {int(self.caught)} "
                f"title {int(self.title)} paused {self.paused} menu {self.menu} cd {self.cd}")


class State:
    def __init__(self, frame):
        self.frame = frame
        self.p = [None, None]

    def __repr__(self):
        return f"f{self.frame} P1 {self.p[0]} | P2 {self.p[1]}"


class Builder:
    def __init__(self, header):
        self.header = header if header.endswith("\n") else header + "\n"
        self.runs = []                  # (frames, input text)

    def text(self):
        return self.header + "".join(f"{n} {inp}\n" for n, inp in self.runs if n > 0)

    def frames(self):
        return sum(n for n, _ in self.runs)

    def trace(self, extra=None):
        runs = self.runs + ([extra] if extra else [])
        body = self.header + "".join(f"{n} {inp}\n" for n, inp in runs if n > 0)
        with tempfile.NamedTemporaryFile("w", suffix=".rpl", delete=False) as f:
            f.write(body)
            path = f.name
        try:
            out = subprocess.run([HOST, "--replay", path, "--headless", "--trace"], cwd=ROOT,
                                 capture_output=True, text=True, check=True).stdout
        finally:
            os.unlink(path)
        states = []
        for line in out.splitlines():
            m = LINE.match(line)
            if not m:
                continue
            g = m.groups()
            if g[1] == "1":
                states.append(State(int(g[0])))
            states[-1].p[int(g[1]) - 1] = PlayerState(g)
        return states

    def state(self):
        s = self.trace()
        return s[-1] if s else None

    def hold(self, inp, frames=None, until=None, limit=900):
        """Append inp for a number of frames, or until until(state) is true
        (inclusive of that frame). Raises if the condition never holds."""
        if until is None:
            self.runs.append((frames, inp))
            return self.state()
        start = self.frames()
        states = self.trace((limit, inp))
        for s in states[start:]:
            if until(s):
                self.runs.append((s.frame - start, inp))
                return s
        raise RuntimeError(f"condition not met within {limit} frames of '{inp}'; last {states[-1]}")

    def save(self, path, comment=None):
        with open(path, "w") as f:
            if comment:
                f.write("".join(f"# {l}\n" for l in comment.strip().splitlines()))
            f.write(self.text())
        print(f"wrote {path} ({self.frames()} frames)")
