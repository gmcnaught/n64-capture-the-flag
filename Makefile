# Top-level build.
#
#   make            host + n64 (N64 is the primary target)
#   make host|n64
#   make run        build assets + host, play (keys: platform/host/main.c)
#   make assets     LDtk levels -> build/assets/levels, sprites -> build/assets/{host,n64},
#                   generated sounds -> build/assets/audio
#   make arena      regenerate the tileset from tools/make_tileset.py, and
#                   assets/src/levels/arena.ldtk from tools/make_arena.py only if missing
#   make test       host self-test + every replay in tests/replays (golden hashes)
#   make replay-update  rewrite the expected hash/kos in every replay
#   make check      host self-test + N64 self-test + N64 replay test, read from ares (headless)
#   make deploy     copy the N64 ROM + replay-test ROM to each MiSTer in MISTER_HOSTS (checksum-verified)
#
# N64_INST defaults to ~/opt/libdragon (libdragon, preview branch).
N64_INST ?= $(HOME)/opt/libdragon
export N64_INST

all: host n64

host:
	$(MAKE) -C platform/host

n64: assets
	$(MAKE) -C platform/n64

selftest: host
	$(MAKE) -C platform/host selftest

SELFTEST_EXPECTED := $(shell sed -n 's/.*CORE_SELFTEST_EXPECTED 0x\([0-9a-f]*\)u.*/\1/p' core/selftest.h)

check: host n64
	$(MAKE) -C platform/host selftest
	python3 tools/ares_read.py platform/n64/ctf.z64 \
	  platform/n64/build/ctf.elf g_selftest_hash g_selftest_done \
	  | tee build-check.log
	grep -q "g_selftest_hash=0x$(SELFTEST_EXPECTED)" build-check.log
	grep -q "g_selftest_done=0x600df00d" build-check.log
	$(MAKE) -C platform/n64 test
	python3 tools/ares_read.py --seconds 180 platform/n64/ctf-test.z64 \
	  platform/n64/build-test/ctf-test.elf g_autotest_done g_replays_run \
	  g_replays_passed g_frame_us_max g_busy_us_max g_repeats | tee build-check.log
	grep -q "g_autotest_done=0x600df00d" build-check.log
	@run=$$(sed -n 's/g_replays_run=//p' build-check.log); \
	 passed=$$(sed -n 's/g_replays_passed=//p' build-check.log); \
	 test "$$run" = "$$passed" && test "$$run" != "0x00000000" || { echo "N64 replays: $$passed of $$run passed"; exit 1; }
	@rm -f build-check.log
	@echo "check: host and N64 self-test PASS ($(SELFTEST_EXPECTED)); N64 replays match host"

# Python tools run in a project venv (Pillow).
PYTHON ?= python3.14
VENV_PY := .venv/bin/python

$(VENV_PY): tools/requirements.txt
	$(PYTHON) -m venv .venv
	$(VENV_PY) -m pip install -q -r tools/requirements.txt
	@touch $@

LEVELS := $(wildcard assets/src/levels/*.ldtk)

# Built assets (host build reads build/assets; see platform/host/main.c).
assets: $(VENV_PY)
	@rm -f build/assets/levels/levels.txt
	@for f in $(LEVELS); do $(VENV_PY) tools/ldtk_import.py $$f build/assets/levels || exit 1; done
	$(VENV_PY) tools/build_sprites.py build/assets
	@test build/assets/audio/.stamp -nt tools/make_audio.py || \
	  { $(VENV_PY) tools/make_audio.py build/assets/audio && touch build/assets/audio/.stamp; }
	@$(MAKE) --no-print-directory $(MUSIC_XMS)

# Tracker music: the N64 converts the modules itself (platform/n64/Makefile);
# the host plays them with libxm from build/assets/audio.
MUSIC_XMS := $(patsubst new-assets/music/%,build/assets/audio/%,$(wildcard new-assets/music/*.xm))

build/assets/audio/%.xm: new-assets/music/%.xm
	@mkdir -p $(dir $@)
	cp $< $@

arena: $(VENV_PY)
	$(VENV_PY) tools/make_tileset.py
	@test -e assets/src/levels/arena.ldtk && echo "arena.ldtk kept (edited in LDtk)" || \
	  $(VENV_PY) tools/make_arena.py

run: assets host
	platform/host/build/ctf

REPLAYS := $(wildcard tests/replays/*.rpl)

test: assets host
	$(MAKE) -C platform/host selftest
	@fail=0; for r in $(REPLAYS); do platform/host/build/ctf --replay $$r --headless --check || fail=1; done; exit $$fail

replay-update: assets host
	@for r in $(REPLAYS); do platform/host/build/ctf --replay $$r --headless --update || exit 1; done

MISTER_HOSTS ?= 192.168.20.81 192.168.20.62
MISTER_N64_DIR ?= /media/fat/games/N64

deploy: n64
	$(MAKE) -C platform/n64 test
	@for rom in ctf ctf-test; do \
	  sum=$$(shasum -a 1 platform/n64/$$rom.z64 | cut -d' ' -f1); \
	  for h in $(MISTER_HOSTS); do \
	    scp -q -o BatchMode=yes -o ConnectTimeout=5 platform/n64/$$rom.z64 root@$$h:$(MISTER_N64_DIR)/$$rom.z64 && \
	    remote=$$(ssh -o BatchMode=yes root@$$h "sha1sum $(MISTER_N64_DIR)/$$rom.z64; sync" | cut -d' ' -f1) && \
	    if [ "$$remote" = "$$sum" ]; then echo "$$h: $$rom.z64 ok $$sum"; else echo "$$h: $$rom.z64 checksum mismatch"; exit 1; fi \
	    || exit 1; \
	  done; \
	done

clean:
	$(MAKE) -C platform/host clean
	$(MAKE) -C platform/n64 clean

.PHONY: all host n64 selftest check assets arena run test replay-update deploy clean
