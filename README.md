# n64-capture-the-flag

Two-player, one-room capture-the-flag game for the Nintendo 64 (libdragon),
built on the n64-platformer framework: the same integer-only C99 core
(movement, collision, replays) running on N64 and an SDL3 host build.

Status: playable capture the flag in a Joust-scale arena after the
whiteboard sketch. Each team has 3 flags by its start; steal the other
team's flags and carry them, one at a time, into your goal (the ledge with
three flag stands at the top on your side); all 3 wins the round. Carriers run
slower and cannot dash or attack; a hit carrier drops the flag where it stands and goes back
to its start. Flags at home can only be taken by the other team; dropped
flags (and flags in a goal) by either player; carry your own flag back to
your start area to return it. Players without a flag lose a heart per hit
and respawn after a knockout. Now and then a blue water fruit appears on
the top slab; touching it releases a Bubble Bobble-style stream that runs
down the platforms and carries anyone in it. Players are solid to each other: you can
stand on the other player, who then runs slower and jumps lower, and
landing on a jumping player stops their jump. Ladders still work in the core but the arena
has none (the Dino art has no climb frames).

The game boots to a title screen (Start or Jump begins a match). Every
round opens with a 3, 2, 1, GO! countdown, players frozen until GO!. Start
pauses into a menu: RESUME, CONTROLS (the N64 controller with each
button's action, and tips), RESTART (new match) or QUIT TO TITLE. A round
win shows "RED WINS!" / "BLUE WINS!" and the rounds score over the dimmed
arena for 3 s. Players start facing away from the middle pillar. A dash
leans the dino forward with speed lines and leaves flickering afterimages
in the team color; an attack sweeps a slash in front (or above or below).
Screen text uses a display font drawn in `tools/font.py` (chunky capitals,
smoothed with Scale2x/Scale3x, outlined, tinted per use); small labels use
Roundabout (FeatureKeep, CC0) in the same outline style.

Music (tracker modules in `new-assets/music/`, played by libdragon xm64 on
the N64 and by libxm, vendored in `platform/host/external/libxm`, on the
host): title screen "Napping on a cloud" by congusbongus (OpenGameArt,
CC0); in play "Which Brand Of Mustard Shall I Buy", also by congusbongus
(OpenGameArt, CC0), selected by the arena's LDtk `Music` field (id 1).

## Screen and scale

The N64 runs at 640x240 progressive; sim coordinates are those pixels. A
4:3 display shows 640x240 pixels half as wide as they are tall, so every
square art pixel is drawn 2 sim px wide (sheets are pre-stretched by
`tools/build_sprites.py`). One 8x8 art tile = one 16x8 cell; the arena is
40x30 cells, the content of four 320x240 screens of 16x16 tiles. The HUD
stays inside a 32x12 px action-safe margin and the arena has 2-cell side
walls there, since TVs and scalers crop the edges (overscan). Characters
are Dino Characters at native size (24x24 frames, 15x17 px bodies, about 7%
of the screen height). The host shows the 640x240 view at 640x480. The N64
draws the backdrop and tiles once per level into a buffer and copies it
each frame.

## Build

Needs the libdragon toolchain at `$N64_INST` (default `~/opt/libdragon`, the
same install as n64-platformer, libdragon `preview`), SDL3 (`brew install
sdl3 pkg-config`), Python 3.14, and ares for `make check`.

```sh
make run          # build assets + host, play
make n64          # platform/n64/ctf.z64
make test         # host self-test + golden replays (tests/replays)
make check        # host + N64 self-test and replays in ares (headless)
make deploy       # copy ROMs to the MiSTers in MISTER_HOSTS
make arena        # regenerate the tileset; arena.ldtk only if missing (it is edited in LDtk)
```

Levels are authored in LDtk (`assets/src/levels/arena.ldtk`, 8 px grid in
art px; the importer doubles entity x). Replays are scripted with
`tools/make_replays.py` on routes from `tools/routefind.c` (a search over
jumps with the real movement code, built by `make host`), so after a level
edit `make assets` and the script re-route everything, and `capture` /
`capture_blue` fail if a team cannot win; then `make replay-update`
records hashes.

## Controls

| Action | P1 keys | P2 keys | Gamepad / N64 |
|---|---|---|---|
| Move (down + jump: drop through a one-way ledge) | WASD | arrows | stick, D-pad |
| Jump | Space | Right Shift | A (south) / A |
| Attack (hold up/down: up/down attack) | F | / | X (west) / B |
| Dash | Left Shift | . | B (east), RB / Z, R |
| Start / pause (menu: up/down, jump or attack picks) | Enter | | Start |

Host debug keys: Backspace restart, Tab next level, F1 overlay, F2 tuning
panel (F5 writes `core/player_tuning.h`), P freeze, N step, F11 fullscreen.
N64 debug (controller 1): L next level, C-down restart, C-up overlay; hold
Start at boot for the replay test.

## Layout

```
core/              shared sim: body, player, level, game (2 players, PvP), selftest
platform/common/   scene.c (shared renderer), replay.c, audio names
platform/host/     SDL3 build
platform/n64/      libdragon build (rdpq, 640x240)
tools/             asset pipeline (tileset, arena, LDtk import, sprites, audio, replays)
assets/src/        arena.ldtk, arena.png (generated from new-assets/)
new-assets/        source art packs (Tiny Adventure Platformer Forest tiles, Dino Characters,
                   Rocky Roads objects/backdrop, The Pixel Nook mountains; the Bongseng
                   forest backdrop and Tiny Hero Sprites are local only; credits in
                   CREDITS.md)
```
