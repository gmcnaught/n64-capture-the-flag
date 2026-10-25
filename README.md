# Capture the Flag

A fast, fun capture-the-flag game for two to four players on the Nintendo 64. Built
with libdragon and SDL3, so the same game plays on the N64 and on PC.

![Blue steals a red flag; red kicks the carrier and takes the flag home](screenshots/gameplay.gif)

| | |
|---|---|
| ![Title screen](screenshots/title.png) | ![Red carries a blue flag home while blue swings](screenshots/gameplay.png) |
| ![Controls screen in the pause menu](screenshots/controls.png) | ![Round winner banner](screenshots/winner.png) |

## Play

**N64:** download `capture-the-flag.z64` from the
[latest release](https://github.com/gmcnaught/n64-capture-the-flag/releases/latest)
and run it on the MiSTer N64 core or in the ares emulator. Plug in one to
four controllers (the computer can play the others).

**PC:** build it from source (below) and run `make run`. Two players share
the keyboard, or plug in gamepads (players 3 and 4: gamepads 3 and 4).

## How to play

Red against blue in one arena, one or two players a team (P3, light red,
plays with P1; P4, dark blue, with P2; teammates can't hit each other).
Each team keeps three flags by its start.

- **Steal** the other team's flags and carry them, one at a time, up to
  your goal ledge. Get all three there to win the round. The first team
  to win three rounds wins the match.
- **Carriers** run slower and can't dash or attack. Kick a carrier and they
  drop the flag and go back to their start.
- **Defend:** grab your own dropped flag and carry it home.
- **Fight:** kick to knock off hearts; lose them all and you respawn.
- **Stack:** stand on another player to weigh them down, or land on them to
  stop their jump.
- **Water fruit:** now and then a fruit appears up top. Touch it and a
  stream of water pours down the platforms, sweeping up anyone in its way.
- **CPU players:** the computer can play the last players (the setup
  screen's CPU row). A CPU player steals flags and takes them to its goal,
  brings its own flags back from your goal, chases and kicks whoever
  carries its flags, and guards its flags when you come near. It is
  beatable on purpose: it reacts late, kicks only now and then, gives up
  a long chase, never dashes, and sometimes misses the jump to its goal.

Every round starts with a 3, 2, 1, GO! countdown. Press Start to pause; the
pause menu has a CONTROLS screen. Start on the title screen opens the
match setup: pick the stage (Arena, Forest, Desert, Snow, Lava, Sky, Factory),
the players (1 VS 1, 2 VS 1, 2 VS 2) and which of them the computer plays
(CPU: none, or the last one, two or three: P2 alone in 1 VS 1, P3 P4 for
a CPU partner each in 2 VS 2) with up/down and left/right, then Start.
On Snow, the ground is ice: you speed up and slow down slowly
there, so a dash or a run slides on. On Desert, a dust storm
hangs over each of the two long side platforms and every few seconds
blows outward, pushing players and dropped flags off toward the walls;
the dust thickens and a whoosh sounds just before it blows. On Sky,
platforms move: clouds slide along the sides and carry whoever stands on
them (jump up through them, drop down with Down + A); two blocks either
side of the pillar go up and down, lifts for anyone on top; two blocks
slide from the side walls to the pillar and back, shoving players
aside. A player crushed
against a wall or floor goes back to its start without losing a heart,
dropping any flag it carries. On Lava, a geyser over each floor bump
bubbles, then erupts every few seconds, throwing anyone standing in it
(and dropped flags) high up out of the low ground. On Factory, conveyor belts
keep running under the flags (toward the wall), on the long side
platforms and the small ones above the middle block (toward the middle),
on top of the middle block (outward) and on the low platforms by the
pillar (toward it): they carry whoever stands on them, and dropped flags,
but not anyone in the air.

## Controls

| Action | N64 | Gamepad | P1 keys | P2 keys |
|---|---|---|---|---|
| Move | Stick, D-pad | Stick, D-pad | WASD | Arrows |
| Jump (again in the air: double jump; at a wall: wall jump) | A | A (south) | Space | Right Shift |
| Attack (hold up or down to aim) | B | X (west) | F | / |
| Dash | Z, R | B (east), RB | Left Shift | . |
| Drop through a platform | Down + A | Down + A | S + Space | Down + Right Shift |
| Pause / menu | Start | Start | Enter | |

## Build

You need the libdragon toolchain (`preview` branch) at `$N64_INST`
(default `~/opt/libdragon`), SDL3 (`brew install sdl3 pkg-config` on
macOS), Python 3, and ares for the N64 tests.

```sh
make run          # build and play the PC version
make n64          # build the N64 ROM: platform/n64/ctf.z64
make test         # PC self-test and replay tests
make check        # the same tests on the N64, in ares: the replays the changes call for
make check-full   # every replay on the N64 (make check ONLY="menu duel": just those)
make deploy MISTER_HOSTS=<ip>   # copy the ROM to a MiSTer
```

The backdrops (forest, desert, moon and sky, picked on the title screen)
come from Bongseng's
[Parallax (Forest, desert, sky, moon)](https://bongseng.itch.io/parallax-forest-desert-sky-moon),
whose files can't be redistributed. Download it and unpack it into
`new-assets/more-parallax/` to build with it; without it every backdrop
is a mountain scene that is included.

The stages are levels in one [LDtk](https://ldtk.io) project
(`assets/src/levels/arena.ldtk`); each has its own layout, a Tileset field
and a Backdrop field. The ground tiles are built from the Collision layer
at import (`tools/terrain.py`: corners, edges and fill from each cell's
neighbors), so LDtk's own tile layers only show in the editor.

## How it works

One integer-only C99 core runs the game on both platforms: movement,
collision, flags and rounds. CPU players (`core/bot.c`) find their way
with A* over a graph of each stage's standing spots and the jumps, falls
and platform rides between them, found by running the real movement code
from every spot at build time (`tools/navbuild.c`, a `.nav` file beside
each level); they press buttons like a player, so replays record them. The N64 build draws it with libdragon at
640x240; the PC build draws the same frames with SDL3. Recorded replays
must produce identical results on PC and N64, which `make test` and
`make check` verify. Music is tracker modules, played by libdragon's xm64
player on the N64 and by libxm on PC.

```
core/              game logic shared by both platforms
platform/common/   shared renderer, replays, audio names
platform/host/     PC build (SDL3)
platform/n64/      N64 build (libdragon)
tools/             asset pipeline, level import, replay scripting
assets/src/        level and tileset
new-assets/        source art and music
tests/replays/     replay tests
```

Debug keys on PC: F1 collision overlay, F2 tuning panel, Backspace
restart, P freeze, N step a frame, F11 fullscreen.

## License

The code is under the [MIT License](LICENSE). Art, music and fonts are by
their creators under their own licenses; see [CREDITS.md](CREDITS.md).
