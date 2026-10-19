#!/usr/bin/env python3
"""Script the golden replays in tests/replays (tools/replay_builder.py).

usage: make_replays.py [NAME ...]   (then `make replay-update` to record hashes)

Writes every scenario below, or only the named ones. Routes come from the
route finder (tools/routefind.c, built by `make host`) on the replay's
level in build/assets/levels, so they follow level edits; every landing
on the way is checked against the route. A level where a team cannot
capture all three flags fails its `capture` / `capture_blue`.

Every scenario first waits out the round countdown (3, 2, 1, GO!).

  capture       P1 goes to the blue flags and carries each (at carry_speed)
                to the red goal; the third one wins the round, and the next
                round starts after its countdown.
  capture_blue  The same for P2: the red flags into the blue goal.
  steal_drop    P1 waits right of the red flags; P2 takes one;
                P1 walks up and kicks the carrier: the flag drops where P2
                stood and P2 goes back to its start; P1 picks up its own
                dropped flag, which returns home in the home area.
  duel          P2 comes over to P1's side and stands left of P1, who is
                against the pillar; P2 kicks P1 until P1 is knocked out; P1
                respawns. Expects kos 0 1.
  stack         P1 stands on the middle landing, P2 walks up to it from the
                right (and stops against P1); P2 hops onto P1's head; P1's
                weighed jump rises at most 24 px and lifts P2; P2 hops and
                lands on P1 while P1 is rising, which stops P1's jump; P1
                walks at weight_speed out from under P2, who drops off.
  water         P2 waits on the top slab between the fruit spots and P1 on
                the right row 15 slab until the first water fruit appears
                (spot 1); P2 touches it facing right; the stream
                runs down the right platforms, catches P1 and carries it
                down; P1 is put down lower when it drains.
  menu          Held input moves nothing during the countdown. P1 walks
                left and pauses; held input moves nothing while paused; P1
                opens CONTROLS (down, jump), where input moves nothing, and
                P2's attack goes back to the menu; P1 and P2 move the cursor
                (down, down, up) and P1 picks RESTART: players back at their
                starts, unpaused, countdown again. P2 pauses, the cursor
                wraps up to QUIT TO TITLE and P2 picks it with attack: title
                screen, where input moves nothing; P2's jump opens the setup
                screen, P1's attack goes back to the title and P1's Start
                opens it again; P1 moves to PLAYERS and right, right makes it
                3 then 4 players; Start starts a 4-player match, which moves
                after its countdown.
  lava_geyser   Lava stage (a geyser over each floor bump). P1 stands on
                the left bump; the eruption lifts it at least 48 px, and it
                comes down again.
  lava_capture  capture on the Lava stage: routes are followed between
                eruptions, so each team can win there.
  lava_capture_blue  capture_blue on the Lava stage.
  team_hits     Arena, 4 players (P3 by P1, P4 by P2). P3 faces P1 and
                swings: P1 keeps its pips (no hits on teammates), as does
                P2 from P4. P4 steps aside, P2 comes over onto the bump left
                of P3 and swings: P3 loses a pip.
  team_flags    Arena, 4 players. As steal_drop with the second players: P3
                waits right of the red flags; P4 takes one; P3 kicks the
                carrier: the flag drops and P4 goes back to its own start;
                P3 picks up its team's dropped flag, which returns home.
  ice_slide     Snow stage (the floor is ice). P1 walks left from its
                start for 16 frames, still under 1.2 px/frame (plain floor:
                full speed 2.7 in 6 frames), lets go and takes at least 15
                frames to stop.
  snow_capture  capture on the Snow stage (ice floor by the flags, ice
                slabs): each team can still win.
  snow_capture_blue  capture_blue on the Snow stage.
  wind_gust     Desert stage (a dust storm over each row 14 platform,
                blowing outward). P1 stands on the left platform, P2 on the
                right one: the gust blows each off its outer end.
  wind_drop     Desert stage. P2 takes a red flag and stands on the left
                storm platform; P1 stands right of it and kicks it just
                before the gust, then walks right: the dropped flag is
                blown left at least 20 px.
  desert_capture  capture on the Desert stage: routes are followed between
                gusts (route, wait_out_gust), so each team can win there.
  desert_capture_blue  capture_blue on the Desert stage.
  platform_ride  Sky stage. P1 lands on the left cloud (one-way, sliding
                along row 18) and is carried at least 48 px standing still;
                P2 gets onto the right middle block (solid, going up and
                down by the pillar) and is carried at least 48 px.
  platform_squish  Sky stage. P1 stands against the left wall by the red
                flags and jumps into the left row 22 block as it slides at
                the wall: squished, P1 is back at its start with all pips.
                P2 takes a red flag and does the same: the flag drops and
                P2 is back at its start with all pips; the flag still lies
                there when the block has slid away.
  platform_push  Sky stage. P1 stands on the row 23 wall right of the red
                flags; the left row 22 block (solid, sliding from the left
                wall to the pillar and back) pushes it at least 16 px, off
                the wall top.
  sky_capture   capture on the Sky stage: routes include riding the
                platforms (routefind runs the whole game there), so each
                team can win.
  sky_capture_blue  capture_blue on the Sky stage.
"""
import re
import os
import subprocess
import sys
import tempfile

from replay_builder import ROOT, Builder

HEADER = "level arena.lvl\nabilities 7\n"
# The rules replays keep the water fruit away (fruit_min 60000 frames).
NO_FRUIT = HEADER + "tune fruit_min 60000\n"
OUT = os.path.join(ROOT, "tests", "replays")
ROUTEFIND = os.path.join(ROOT, "platform", "host", "build", "routefind")
LEVELS = os.path.join(ROOT, "build", "assets", "levels")
FLAGS = {0: [0, 1, 2], 1: [3, 4, 5]}     # Game.flags indices per team (entity order)
COUNTDOWN = 3 * 60 + 40                 # COUNTDOWN_FRAMES (core/game.h)
COUNTDOWN_GO = 40
PLATFORM_WAIT, PLATFORM_RETRIES = 30, 12   # no route with moving platforms: wait, search again


def inp(side, d):
    """Replay input with direction/buttons d for one player, the others idle."""
    groups = ["C"] * max(2, side + 1)
    groups[side] = d + "C"
    return " / ".join(groups)


def check(cond, msg, state):
    if not cond:
        raise RuntimeError(f"{msg}: {state}")


def countdown(b):
    """Wait out the round countdown (players frozen until GO!, which stays
    on screen into play)."""
    return b.hold("C / C", until=lambda s: s.p[0].cd == 0, limit=COUNTDOWN + 1)


def at_rest(b, side):
    """Stand until at rest, or a round is won (everything stops for the banner)."""
    return b.hold(inp(side, ""), until=lambda s: (s.p[side].vx == 0 and s.p[side].grounded) or
                  s.p[side].winner, limit=90)


def find_route(b, side, target):
    """Route for player side from the end of the replay so far (routefind
    replays it for the exact state; a carrier moves at carry_speed)."""
    with tempfile.NamedTemporaryFile("w", suffix=".rpl", delete=False) as f:
        f.write(b.text())
        path = f.name
    try:
        level = os.path.join(LEVELS, re.search(r"^level (\S+)", b.header, re.M).group(1))
        out = subprocess.run([ROUTEFIND, level, path, str(side), target],
                             capture_output=True, text=True).stdout.split("\n")
    finally:
        os.unlink(path)
    if not out or out[0] in ("", "none"):
        return None
    return [line.split() for line in out if line]


def move_frames(s, side, tx):
    """Generous estimate of a route move's frames (walk to tx, jump, settle)."""
    p = s.p[side]
    return abs(tx - p.x) * 10 // (17 if p.flag >= 0 else 27) + 120


def wait_out_gust(b, side, hit):
    """Stand through the next gust (it may push the player anywhere) and
    come to rest after it; the state if hit(state) on the way, else None."""
    s = b.hold(inp(side, ""), until=lambda s: s.p[side].calm == 0 or hit(s), limit=2000)
    if not hit(s):
        s = b.hold(inp(side, ""), until=lambda s: s.p[side].calm > 0 or hit(s), limit=2000)
    if hit(s):
        return s
    at_rest(b, side)
    return None


def route(b, side, target, done=None):
    """Follow the route finder's route for one player from rest to target
    (see tools/routefind.c for the moves). done(state): the target reached
    in the game (a pickup, a placement), checked on every frame. With wind
    (routefind leaves it out), each move starts only with enough calm
    left for it; otherwise the player waits out the gust and the route is
    found again from where it left the player. With moving platforms, if
    there is no route now, the player stands PLATFORM_WAIT frames and the
    route is searched again (up to PLATFORM_RETRIES times)."""
    while True:
        s = route_calm(b, side, target, done)
        if s is None and done:
            s = wait_out_gust(b, side, done)
        elif s is None:
            wait_out_gust(b, side, lambda s: False)
        if s is not None:
            return s


def route_calm(b, side, target, done):
    """route, up to the first move that would meet a gust (None then)."""
    s = at_rest(b, side)
    moves = find_route(b, side, target)
    for _ in range(PLATFORM_RETRIES):
        if moves is not None or not s.p[side].pl:
            break
        # Moving platforms: routefind's moves start now, so wait a little for
        # them to move on and search again.
        s = b.hold(inp(side, ""), PLATFORM_WAIT)
        moves = find_route(b, side, target)
    if moves is None:
        raise RuntimeError(f"no route for P{side + 1} to {target}: {b.state()}")
    hit = done or (lambda s: False)
    for i, (d, tx, kind, air, ex, ey) in enumerate(moves):
        tx, ex, ey = int(tx), int(ex), int(ey)
        if s.p[side].calm < move_frames(s, side, tx):
            return None
        b.hold(inp(side, ""), 1)
        if d == "L":
            s = b.hold(inp(side, d), until=lambda s: s.p[side].x <= tx or hit(s), limit=400)
        else:
            s = b.hold(inp(side, d), until=lambda s: s.p[side].x >= tx or hit(s), limit=400)
        if not hit(s):
            a = "" if air == "-" else d
            if kind == "drop":
                b.hold(inp(side, "D"), 2)
                b.hold(inp(side, "DJ"), 2)
            double = kind[0] == "d" and kind[1:].isdigit()     # dK
            jump = "J" if kind == "jump" or double else ""
            first = int(kind[1:]) - 1 if double else 2
            start = b.frames()
            s = b.hold(inp(side, a + jump),
                       until=lambda s: hit(s) or s.frame >= start + first, limit=first + 1)
            if double and not hit(s):
                s = b.hold(inp(side, a), 1)
            if not hit(s):
                s = b.hold(inp(side, a + jump), until=lambda s: hit(s) or s.p[side].grounded,
                           limit=400)
            if not hit(s) and (s.p[side].vx != 0 or not s.p[side].grounded):
                s = at_rest(b, side)    # (routefind takes no frame when landed at rest)
        if hit(s):
            return s
        p = s.p[side]
        if target.startswith("rest:") and i == len(moves) - 1:
            x0, x1, y = map(int, target[5:].split(","))
            check(x0 <= p.x <= x1 and p.y == y, f"P{side + 1} not at {target}", s)
            return s
        check((p.x, p.y) == (ex, ey), f"P{side + 1} off route at move {i + 1} {moves[i]}", s)
    check(done is None, f"P{side + 1} reached the end of the route without {target}", s)
    return s


def desert_capture():
    return capture_team(0, "desert.lvl")


def desert_capture_blue():
    return capture_team(1, "desert.lvl")


# Desert storm platforms (row 14 slabs), where a player stands on them.
LEFT_SLAB, RIGHT_SLAB, SLAB_Y = (64, 136), (480, 552), 96


def wind_gust():
    b = Builder(NO_FRUIT.replace("arena.lvl", "desert.lvl"))
    countdown(b)
    b.hold("C / C", 10)
    route(b, 0, f"rest:88,112,{SLAB_Y}")
    route(b, 1, f"rest:504,528,{SLAB_Y}")
    s = b.hold("C / C", until=lambda s: s.p[0].calm == 0, limit=600)
    x = [s.p[0].x, s.p[1].x]
    s = b.hold("C / C", until=lambda s: s.p[0].calm > 0, limit=600)
    check(x[0] - s.p[0].x >= 40 or s.p[0].y > SLAB_Y, "P1 not blown left off its platform", s)
    check(s.p[1].x - x[1] >= 40 or s.p[1].y > SLAB_Y, "P2 not blown right off its platform", s)
    b.hold("C / C", 30)
    return b


def wind_drop():
    b = Builder(NO_FRUIT.replace("arena.lvl", "desert.lvl"))
    countdown(b)
    b.hold("C / C", 10)
    s = route(b, 1, f"flag:{FLAGS[0][0]}", done=lambda s: s.p[1].flag >= 0)
    check(s.p[1].flag in FLAGS[0], "P2 took no red flag", s)
    wait_out_gust(b, 1, lambda s: False)                    # a whole calm for the setup
    route(b, 1, f"rest:72,88,{SLAB_Y}")                     # carrier on the left storm platform
    s = route(b, 0, f"rest:112,128,{SLAB_Y}")               # P1 right of it
    check(s.p[0].calm > 20 and s.p[1].y == SLAB_Y, "setup ran into the gust", s)
    b.hold("C / C", until=lambda s: s.p[0].calm == 20, limit=600)
    b.hold(inp(0, "L"), until=lambda s: s.p[0].x - s.p[1].x <= 34, limit=60)
    b.hold("AC / C", 2)                                     # P1 kicks the carrier
    s = b.hold("C / C", until=lambda s: s.p[1].flag < 0, limit=20)
    check(s.p[0].dfx >= 0, "no flag dropped", s)
    s = b.hold("C / C", until=lambda s: s.p[0].calm == 0 and s.p[0].dfx >= 0, limit=60)
    x = s.p[0].dfx
    s = b.hold(inp(0, "R"), until=lambda s: s.p[0].calm > 0, limit=300)   # P1 keeps off it
    check(s.p[0].dfx >= 0 and x - s.p[0].dfx >= 20, "dropped flag not blown left", s)
    b.hold("C / C", 30)
    return b


SKY = NO_FRUIT.replace("arena.lvl", "sky.lvl")
# Sky stage: Game.platforms indices (entity order: y, then x) and where a
# player stands (body top-left y) on its parts.
LEFT_BLOCK, RIGHT_BLOCK, LEFT_CLOUD, RIGHT_CLOUD, LEFT_PUSHER, RIGHT_PUSHER = range(6)
CLOUD_Y, WALL_TOP_Y, FLOOR_Y = 128, 168, 200    # WALL_TOP_Y: on the row 23 wall
PLAYER_W = 24                   # core/player.h PLAYER_HIT_W
WALL_X = 32                     # against the left wall, by the red flags, where the left
                                # row 22 block (the pusher) ends its slide


def platform_ride():
    b = Builder(SKY)
    countdown(b)
    b.hold("C / C", 10)
    s = route(b, 0, f"ride:{LEFT_CLOUD}")
    x = s.p[0].x
    s = b.hold("C / C", until=lambda s: abs(s.p[0].x - x) >= 48 or s.p[0].ride != LEFT_CLOUD,
               limit=300)
    check(s.p[0].ride == LEFT_CLOUD and s.p[0].y == CLOUD_Y, "P1 not carried by the cloud", s)
    s = route(b, 1, f"ride:{RIGHT_BLOCK}")
    seen = [s.p[1].y, s.p[1].y]           # highest and lowest y while riding

    def rode(s):
        seen[0], seen[1] = min(seen[0], s.p[1].y), max(seen[1], s.p[1].y)
        return s.p[1].ride != RIGHT_BLOCK or seen[1] - seen[0] >= 48
    s = b.hold("C / C", until=rode, limit=400)
    check(s.p[1].ride == RIGHT_BLOCK, "P2 not carried 48 px by the right block", s)
    b.hold("C / C", 30)
    return b


def squished(b, side, home):
    """Player side stands against the left wall by the red flags and jumps
    as the left pusher slides at it: pushed into the wall, it is back at
    its start (home) with all pips."""
    s = route(b, side, f"rest:{WALL_X},{WALL_X + 4},{FLOOR_Y}")
    hp, x = s.p[side].hp, s.p[side].x
    prev = [s.p[0].pl[LEFT_PUSHER][0]]

    def arriving(s):                    # sliding left, 10 px from the player: it meets
        px = s.p[0].pl[LEFT_PUSHER][0]  # the jump in the block's rows
        moving_left, prev[0] = px < prev[0], px
        return moving_left and px == x + PLAYER_W + 10
    b.hold("C / C", until=arriving, limit=900)
    s = b.hold(inp(side, "J"), until=lambda s: (s.p[side].x, s.p[side].y) == home, limit=30)
    check(s.p[side].hp == hp, f"P{side + 1} lost a pip to the squish", s)
    return s


def platform_squish():
    b = Builder(SKY)
    first = b.hold("C / C", 1)
    countdown(b)
    b.hold("C / C", 10)
    home = [(first.p[t].x, first.p[t].y) for t in range(2)]
    squished(b, 0, home[0])
    b.hold("C / C", 20)
    s = route(b, 1, f"flag:{FLAGS[0][0]}", done=lambda s: s.p[1].flag >= 0)
    check(s.p[1].flag in FLAGS[0], "P2 took no red flag", s)
    s = squished(b, 1, home[1])
    check(s.p[1].flag < 0 and s.p[1].dfx >= 0, "carrier's flag not dropped", s)
    s = b.hold("C / C", until=lambda s: s.p[1].dfx < 0 or s.p[0].pl[LEFT_PUSHER][0] > WALL_X + 48,
               limit=200)
    check(s.p[1].dfx >= 0, "dropped flag gone with the pusher", s)
    b.hold("C / C", 30)
    return b


def platform_push():
    b = Builder(SKY)
    countdown(b)
    b.hold("C / C", 10)
    s = route(b, 0, f"rest:96,112,{WALL_TOP_Y}")
    x = s.p[0].x
    s = b.hold("C / C", until=lambda s: s.p[0].y > WALL_TOP_Y and s.p[0].grounded, limit=600)
    check(abs(s.p[0].x - x) >= 16, "P1 not pushed off the wall top", s)
    b.hold("C / C", 30)
    return b


def lava_geyser():
    b = Builder(NO_FRUIT.replace("arena.lvl", "lava.lvl"))
    countdown(b)
    b.hold("C / C", 10)
    s = route(b, 0, "rest:208,216,192")                     # on the left bump
    low = [s.p[0].y]

    def up(s):
        low[0] = min(low[0], s.p[0].y)
        return s.p[0].calm == 0 and low[0] <= 192 - 48
    s = b.hold("C / C", until=up, limit=600)
    s = b.hold("C / C", until=lambda s: s.p[0].grounded and s.p[0].vy >= 0 and s.p[0].calm > 0,
               limit=400)
    check(low[0] <= 192 - 48, "P1 not lifted 48 px by the geyser", s)
    b.hold("C / C", 30)
    return b


def lava_capture():
    return capture_team(0, "lava.lvl")


def lava_capture_blue():
    return capture_team(1, "lava.lvl")


def sky_capture():
    return capture_team(0, "sky.lvl")


def sky_capture_blue():
    return capture_team(1, "sky.lvl")


def capture_team(side, level="arena.lvl"):
    """Player side captures the other team's three flags into its goal."""
    b = Builder(NO_FRUIT.replace("arena.lvl", level))
    countdown(b)
    b.hold("C / C", 10)
    left = list(FLAGS[1 - side])
    goal = f"goal{side + 1}"
    for _ in range(3):
        # Nearest flag still at home (fewest moves).
        at_rest(b, side)
        best = min(left, key=lambda i: len(find_route(b, side, f"flag:{i}") or [0] * 99))
        s = route(b, side, f"flag:{best}", done=lambda s: s.p[side].flag >= 0)
        left.remove(s.p[side].flag)
        s = route(b, side, goal, done=lambda s: s.p[side].flag < 0)
        if s.p[side].winner:
            break
    else:
        raise RuntimeError(f"no round win: {b.state()}")
    check(s.p[side].winner == side + 1, "wrong winner", s)
    b.hold("C / C", until=lambda s: s.p[0].winner == 0, limit=400)
    s = countdown(b)
    check(s.p[0].wins == (side == 0) and s.p[1].wins == (side == 1), "round win not counted", s)
    b.hold("C / C", 30)
    return b


def capture():
    return capture_team(0)


def capture_blue():
    return capture_team(1)


def snow_capture():
    return capture_team(0, "snow.lvl")


def snow_capture_blue():
    return capture_team(1, "snow.lvl")


def ice_slide():
    b = Builder(NO_FRUIT.replace("arena.lvl", "snow.lvl"))
    countdown(b)
    s = b.hold("LC / C", 16)                # P1 starts on the ice floor
    check(-1.2 < s.p[0].vx < 0, "P1 sped up quickly on the ice", s)
    start = s.frame
    s = b.hold("C / C", until=lambda s: s.p[0].vx == 0, limit=60)
    check(s.frame - start >= 15, "P1 stopped quickly on the ice", s)
    b.hold("C / C", 60)
    return b


def steal_drop():
    b = Builder(NO_FRUIT)
    countdown(b)
    b.hold("C / C", 10)
    route(b, 0, "rest:136,152,200")                         # P1 right of the red flags
    s = route(b, 1, f"flag:{FLAGS[0][0]}", done=lambda s: s.p[1].flag >= 0)
    check(s.p[1].flag in FLAGS[0], "P2 took no red flag", s)   # may grab one on the way
    at_rest(b, 1)
    b.hold(inp(0, "L"), until=lambda s: s.p[0].x - s.p[1].x <= 34, limit=200)
    b.hold("AC / C", 2)                                     # P1 kicks the carrier
    s = b.hold("C / C", until=lambda s: s.p[1].flag < 0, limit=20)
    check(s.p[1].x >= 320, "carrier not sent home", s)
    b.hold("C / C", 20)
    b.hold(inp(0, "L"), until=lambda s: s.p[0].flag >= 0, limit=120)
    b.hold("C / C", until=lambda s: s.p[0].flag < 0, limit=10)   # returned home
    b.hold("C / C", 30)
    return b


def duel():
    b = Builder(NO_FRUIT)
    countdown(b)
    b.hold("C / C", 10)
    s = route(b, 1, "rest:244,254,200")                     # left of P1 (against the pillar)
    b.hold(inp(1, "R"), 1)                                  # face P1
    at_rest(b, 1)
    for _ in range(40):             # swing, then close in again
        if b.state().p[0].dead:
            break
        b.hold("C / AC", 2)
        b.hold("C / C", until=lambda s: s.p[1].atk == 0, limit=60)
        if b.state().p[0].x - b.state().p[1].x > 34:
            b.hold("C / RC", until=lambda s: s.p[0].x - s.p[1].x <= 34, limit=120)
        b.hold("C / C", 8)
    else:
        raise RuntimeError(f"no knockout: {b.state()}")
    b.hold("C / C", until=lambda s: not s.p[0].dead, limit=200)
    b.hold("C / C", 30)
    return b


def on_head(s):
    """P2 stands on P1."""
    return s.p[1].y + 16 == s.p[0].y and abs(s.p[1].x - s.p[0].x) < 24


def stack():
    b = Builder(NO_FRUIT)
    countdown(b)
    b.hold("C / C", 10)
    route(b, 0, "rest:300,316,96")                          # P1 on the middle landing
    s = route(b, 1, "rest:340,372,96")                      # P2 right of it
    s = b.hold(inp(1, "L"), until=lambda s: s.p[1].x <= s.p[0].x + 24, limit=60)
    s = at_rest(b, 1)
    check(s.p[1].x == s.p[0].x + 24, "P2 not stopped against P1", s)
    # P2 hops onto P1's head.
    b.hold("C / LJC", until=lambda s: s.p[1].x <= s.p[0].x + 12 or s.p[1].vy >= 0)
    b.hold("C / JC", until=lambda s: s.p[1].vy >= 0, limit=30)
    s = b.hold("C / C", until=lambda s: s.p[1].grounded, limit=60)
    check(on_head(s), "P2 not standing on P1", s)
    b.hold("C / C", 10)
    # Weighed jump: lower (weight_jump 3.0, about 18 px), lifting the rider.
    start = b.frames()
    b.hold("JC / C", 20)
    s = b.hold("C / C", until=lambda s: s.p[0].grounded and s.p[0].vy >= 0, limit=60)
    states = b.trace()[start:]
    rise = states[0].p[0].y - min(t.p[0].y for t in states)
    check(0 < rise <= 24, f"weighed jump rose {rise} px", s)
    check(all(on_head(t) for t in states if t.p[0].vy < 0), "rider not lifted with P1", s)
    s = b.hold("C / C", until=lambda s: s.p[1].grounded, limit=60)
    check(on_head(s), "rider not back on P1 after the jump", s)
    # A rider landing on a rising player stops that jump: P2 hops (a tap,
    # staying under the top slab), P1 jumps under it as it comes down.
    b.hold("C / JC", 1)
    b.hold("C / C", until=lambda s: s.p[1].vy > 0 and s.p[1].y + 16 <= s.p[0].y - 3, limit=40)
    start = b.frames()
    b.hold("JC / C", 12)
    states = b.trace()[start - 1:]
    meet = next((i for i in range(1, len(states))
                 if on_head(states[i]) and not on_head(states[i - 1])), None)
    check(meet is not None and states[meet - 1].p[0].vy < 0 and states[meet].p[0].vy >= 0,
          "P1's jump not stopped by the rider landing", states[meet] if meet else states[-1])
    b.hold("C / C", until=lambda s: s.p[0].grounded and s.p[1].grounded, limit=60)
    # Weighed walk, out from under the rider, who drops off.
    s = b.hold("RC / C", 12)
    check(s.p[0].vx == 1.9, "P1 not slowed by the rider", s)
    s = b.hold("RC / C", until=lambda s: s.p[1].y == s.p[0].y, limit=60)
    b.hold("C / C", 30)
    return b


def water():
    b = Builder(HEADER)
    countdown(b)
    b.hold("C / C", 10)
    # P2 onto the top slab between fruit spots 0 (x 288) and 1 (x 336),
    # touching neither; P1 onto the right row 15 slab (on the stream's way).
    route(b, 1, "rest:300,312,56")
    route(b, 0, "rest:432,440,104")
    s = b.hold("C / C", until=lambda s: s.p[0].fruit == 1, limit=1600)
    check(s.p[0].spot == 1, "first fruit not at spot 1", s)
    s = b.hold("C / RC", until=lambda s: s.p[0].water > 0, limit=60)   # P2 touches it facing right
    b.hold("C / LC", 16)                                    # and steps back out of the stream
    s = b.hold("C / C", until=lambda s: s.p[0].caught, limit=200)
    check(96 <= s.p[0].y <= 104 and 420 <= s.p[0].x <= 450 and not s.p[1].caught,
          "P1 not caught at the row 15 slab", s)
    s = b.hold("C / C", until=lambda s: s.p[0].water == 0, limit=400)
    check(not s.p[0].caught and s.p[0].y > 104, "P1 not put down lower when the stream drained", s)
    b.hold("C / C", 30)
    return b


def menu():
    b = Builder(NO_FRUIT)
    first = b.hold("C / C", 1)
    s = b.hold("LC / RC", 60)
    check(s.p[0].cd > COUNTDOWN_GO and (s.p[0].x, s.p[1].x) == (first.p[0].x, first.p[1].x),
          "players moved during the countdown", s)
    countdown(b)
    b.hold("C / C", 10)
    start = b.state()
    s = b.hold("LC / C", 30)
    check(s.p[0].x < start.p[0].x, "P1 did not walk", s)
    paused = b.hold("SC / C", 1)
    check(paused.p[0].paused == 1 and paused.p[0].menu == 0, "Start did not pause", paused)
    s = b.hold("LC / RC", 20)
    check((s.p[0].x, s.p[0].y, s.p[1].x) == (paused.p[0].x, paused.p[0].y, paused.p[1].x),
          "players moved while paused", s)
    b.hold("C / C", 1)
    s = b.hold("DC / C", 1)
    check(s.p[0].menu == 1, "cursor not on CONTROLS after down", s)
    b.hold("C / C", 1)
    s = b.hold("JC / C", 1)
    check(s.p[0].paused == 2, "CONTROLS did not open", s)
    b.hold("C / C", 1)
    s = b.hold("DC / UC", 20)
    check(s.p[0].paused == 2 and s.p[0].menu == 1 and s.p[0].x == paused.p[0].x,
          "input acted on the controls screen", s)
    b.hold("C / C", 1)
    s = b.hold("C / AC", 1)
    check(s.p[0].paused == 1 and s.p[0].menu == 1, "attack did not go back to the menu", s)
    b.hold("C / C", 1)
    b.hold("C / DC", 1)
    b.hold("C / C", 1)
    s = b.hold("DC / C", 1)
    check(s.p[0].menu == 3, "cursor not on QUIT after down, down", s)
    s = b.hold("C / UC", 1)
    check(s.p[0].menu == 2, "cursor not on RESTART after up", s)
    s = b.hold("JC / C", 1)
    check(not s.p[0].paused and (s.p[0].x, s.p[0].y) == (start.p[0].x, start.p[0].y) and
          s.p[0].cd == COUNTDOWN, "RESTART did not restart the round", s)
    countdown(b)
    b.hold("C / C", 10)
    s = b.hold("C / SC", 1)
    check(s.p[0].paused == 1 and s.p[0].menu == 0, "P2's Start did not pause", s)
    b.hold("C / C", 1)
    s = b.hold("C / UC", 1)
    check(s.p[0].menu == 3, "cursor did not wrap up to QUIT", s)
    s = b.hold("C / AC", 1)
    check(s.p[0].title == 1 and not s.p[0].paused, "QUIT did not show the title screen", s)
    s = b.hold("LC / RC", 20)
    check((s.p[0].x, s.p[1].x) == (start.p[0].x, start.p[1].x), "players moved on the title", s)
    b.hold("C / C", 1)
    s = b.hold("C / JC", 1)
    check(s.p[0].title == 2, "jump did not open the setup screen", s)
    b.hold("C / C", 1)
    s = b.hold("AC / C", 1)
    check(s.p[0].title == 1, "attack did not go back to the title", s)
    b.hold("C / C", 1)
    s = b.hold("SC / C", 1)
    check(s.p[0].title == 2, "Start did not open the setup screen", s)
    b.hold("C / C", 1)
    b.hold("DC / C", 1)                                     # PLAYERS row
    b.hold("C / C", 1)
    s = b.hold("RC / C", 1)
    check(s.p[2] and not s.p[3], "right did not make it 3 players", s)
    b.hold("C / C", 1)
    s = b.hold("RC / C", 1)
    check(s.p[3], "right did not make it 4 players", s)
    b.hold("C / C", 1)
    s = b.hold("SC / C", 1)
    check(not s.p[0].title and s.p[3] and s.p[0].cd == COUNTDOWN, "Start did not start a match", s)
    countdown(b)
    s = b.hold("LC / RC", 20)
    check(s.p[0].x < start.p[0].x, "P1 cannot move after the setup screen", s)
    return b


FOUR = NO_FRUIT + "players 4\n"


def team_hits():
    b = Builder(FOUR)
    countdown(b)
    b.hold("C / C", 10)
    for mate, side in ((2, 0), (3, 1)):                    # P3 at P1, P4 at P2
        s = b.state()
        toward = "R" if s.p[side].x > s.p[mate].x else "L"
        b.hold(inp(mate, toward), 1)
        at_rest(b, mate)
        b.hold(inp(mate, "A"), 2)
        s = b.hold(inp(mate, ""), until=lambda s: s.p[mate].atk == 0, limit=60)
        check(s.p[side].hp == 3 and s.p[side].vx == 0, f"P{mate + 1} hurt its teammate", s)
    route(b, 3, "rest:440,470,200")                         # P4 out of P2's way
    s = route(b, 1, "rest:214,220,192")                     # P2 on the bump left of P3
    b.hold(inp(1, "R"), 1)
    at_rest(b, 1)
    hp = s.p[2].hp
    b.hold(inp(1, "A"), 2)
    s = b.hold(inp(1, ""), until=lambda s: s.p[1].atk == 0, limit=60)
    check(s.p[2].hp == hp - 1, "P2 did not hit P3", s)
    b.hold("C / C", 30)
    return b


def team_flags():
    b = Builder(FOUR)
    first = b.hold("C / C", 1)
    countdown(b)
    b.hold("C / C", 10)
    route(b, 2, "rest:136,152,200")                         # P3 right of the red flags
    s = route(b, 3, f"flag:{FLAGS[0][0]}", done=lambda s: s.p[3].flag >= 0)
    check(s.p[3].flag in FLAGS[0], "P4 took no red flag", s)
    at_rest(b, 3)
    b.hold(inp(2, "L"), until=lambda s: s.p[2].x - s.p[3].x <= 34, limit=200)
    b.hold(inp(2, "A"), 2)                                  # P3 kicks the carrier
    s = b.hold("C / C", until=lambda s: s.p[3].flag < 0, limit=20)
    check((s.p[3].x, s.p[3].y) == (first.p[3].x, first.p[3].y) and s.p[3].hp == 3,
          "carrier not sent to its own start", s)
    b.hold("C / C", 20)
    b.hold(inp(2, "L"), until=lambda s: s.p[2].flag >= 0, limit=120)
    s = b.hold("C / C", until=lambda s: s.p[2].flag < 0, limit=10)   # returned home
    check(s.p[2].dfx < 0, "flag not back home", s)
    b.hold("C / C", 30)
    return b


def main():
    os.makedirs(OUT, exist_ok=True)
    docs = __doc__.split("\n  ")
    scenarios = (capture, capture_blue, steal_drop, duel, stack, water, menu, ice_slide,
                 snow_capture, snow_capture_blue, wind_gust, wind_drop, desert_capture,
                 desert_capture_blue, platform_ride, platform_squish, platform_push, sky_capture,
                 sky_capture_blue, lava_geyser, lava_capture, lava_capture_blue, team_hits,
                 team_flags)
    names = sys.argv[1:]
    for fn in scenarios:
        if names and fn.__name__ not in names:
            continue
        comment = next(d for d in docs if d.startswith(fn.__name__ + " "))
        fn().save(os.path.join(OUT, fn.__name__ + ".rpl"), " ".join(comment.split()))


if __name__ == "__main__":
    main()
