ROMNAME := ctf
BUILD_DIR = build
CORE_DIR := ../../core

N64_INST ?= $(HOME)/opt/libdragon
export N64_INST
ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
  $(error $(N64_INST)/include/n64.mk does not exist: build libdragon first (make -C ../.. libdragon))
endif
# 8 MB: the 4 MB console runs out of memory (sprite sheets, framebuffers
# and the composite buffers in src/main.c).
N64_ROM_EXPANSIONPAK = required
include $(N64_INST)/include/n64.mk

COMMON_DIR := ../common
N64_CFLAGS += -I$(CORE_DIR) -I$(COMMON_DIR) $(EXTRA_CFLAGS)

all: $(ROMNAME).z64
.PHONY: all

C_FILES := $(shell find src -name '*.c')
CORE_FILES := $(wildcard $(CORE_DIR)/*.c)
OBJS := $(addprefix $(BUILD_DIR)/,$(C_FILES:.c=.o)) \
        $(patsubst $(CORE_DIR)/%.c,$(BUILD_DIR)/core/%.o,$(CORE_FILES)) \
        $(addprefix $(BUILD_DIR)/common/,replay.o scene.o audio_names.o)

$(BUILD_DIR)/core/%.o: $(CORE_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/common/%.o: $(COMMON_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(ROMNAME).elf: $(OBJS)

# ROM filesystem (rom:/): sprites, worlds and sounds from ../../build/assets,
# which the top-level `make assets` produces.
ASSETS_DIR := ../../build/assets
FS_DIR := filesystem
BACKDROPS := forest desert moon sky
TILESETS := arena rocky_forest rocky_desert rocky_snow rocky_lava
SPRITES := p1 p2 p3 p4 jumppad flag hud fruit font slash controls platform belt $(addprefix tiles_,$(TILESETS)) \
           $(addprefix sky_,$(BACKDROPS)) $(addprefix scenery_,$(BACKDROPS))
MUSIC_DIR := ../../new-assets/music
MODULES := $(basename $(notdir $(wildcard $(MUSIC_DIR)/*.xm)))
# N64 music is modules only: rendered and synthesized music WAVs stay out.
SOUNDS := $(filter-out $(MODULES) music_%,$(basename $(notdir $(wildcard $(ASSETS_DIR)/audio/*.wav))))
LEVEL_FILES := $(wildcard $(ASSETS_DIR)/levels/*.lvl) $(ASSETS_DIR)/levels/levels.txt
REPLAY_FILES := $(wildcard ../../tests/replays/*.rpl)
FS_FILES := $(addprefix $(FS_DIR)/,$(addsuffix .sprite,$(SPRITES))) \
            $(addprefix $(FS_DIR)/,$(addsuffix .wav64,$(SOUNDS))) \
            $(addprefix $(FS_DIR)/,$(addsuffix .xm64,$(MODULES))) \
            $(addprefix $(FS_DIR)/,$(notdir $(LEVEL_FILES) $(REPLAY_FILES))) \
            $(FS_DIR)/replays.txt

# Replay-test ROM only (test, below): the replays it runs.
ifdef CHECK_LIST
FS_FILES += $(FS_DIR)/check.txt
# Rewritten only when the list changes, so an unchanged pick does not repack the ROM.
$(FS_DIR)/check.txt: FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' $(CHECK_REPLAYS) > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@
endif
FORCE:

$(FS_DIR)/%.sprite: $(ASSETS_DIR)/n64/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) --format RGBA16 $(MKSPRITE_FLAGS) -o $(FS_DIR) "$<"

# Textures default to RGBA16; large sheets are compressed at level 1.
$(FS_DIR)/font.sprite \
  $(patsubst %,$(FS_DIR)/sky_%.sprite,$(BACKDROPS)) $(patsubst %,$(FS_DIR)/scenery_%.sprite,$(BACKDROPS)): MKSPRITE_FLAGS = --compress 1

$(FS_DIR)/%.wav64: $(ASSETS_DIR)/audio/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	$(N64_AUDIOCONV) --wav-compress 1 -o $(FS_DIR) "$<"

$(FS_DIR)/%.xm64: $(MUSIC_DIR)/%.xm
	@mkdir -p $(dir $@)
	@echo "    [XM] $@"
	$(N64_AUDIOCONV) -o $(FS_DIR) "$<"

$(FS_DIR)/%: $(ASSETS_DIR)/levels/%
	@mkdir -p $(dir $@)
	cp "$<" "$@"

$(FS_DIR)/%.rpl: ../../tests/replays/%.rpl
	@mkdir -p $(dir $@)
	cp "$<" "$@"

$(FS_DIR)/replays.txt: $(REPLAY_FILES)
	@mkdir -p $(dir $@)
	printf '%s\n' $(notdir $(REPLAY_FILES)) > $@

$(BUILD_DIR)/$(ROMNAME).dfs: $(FS_FILES)

$(ROMNAME).z64: N64_ROM_TITLE = "N64 Capture Flag"
$(ROMNAME).z64: $(BUILD_DIR)/$(ROMNAME).dfs

# Replay-test ROM: runs the replays named in CHECK_REPLAYS (file names in
# tests/replays; default all) at boot, listed in rom:/check.txt (src/main.c).
CHECK_REPLAYS ?= $(notdir $(REPLAY_FILES))
test:
	$(MAKE) ROMNAME=ctf-test BUILD_DIR=build-test EXTRA_CFLAGS=-DAUTOTEST CHECK_LIST=1 \
	  CHECK_REPLAYS="$(CHECK_REPLAYS)"


clean:
	$(RM) -r $(BUILD_DIR) build-test build-log $(FS_DIR) *.z64
.PHONY: clean test FORCE

ifneq ($(wildcard $(BUILD_DIR)),)
  -include $(shell find $(BUILD_DIR) -name '*.d')
endif
