ROMNAME := ctf
BUILD_DIR = build
CORE_DIR := ../../core

N64_INST ?= $(HOME)/opt/libdragon
export N64_INST
ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
  $(error $(N64_INST)/include/n64.mk does not exist: build libdragon first (make -C ../.. libdragon))
endif
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
SPRITES := tiles p1 p2 jumppad flag backdrop hud fruit font slash controls
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

$(FS_DIR)/%.sprite: $(ASSETS_DIR)/n64/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) --format RGBA16 $(MKSPRITE_FLAGS) -o $(FS_DIR) "$<"

# Textures default to RGBA16; large sheets are compressed at level 1.
$(FS_DIR)/tiles.sprite $(FS_DIR)/backdrop.sprite $(FS_DIR)/font.sprite: MKSPRITE_FLAGS = --compress 1

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

# Replay-test ROM: runs every replay at boot (see src/main.c).
test:
	$(MAKE) ROMNAME=ctf-test BUILD_DIR=build-test EXTRA_CFLAGS=-DAUTOTEST

clean:
	$(RM) -r $(BUILD_DIR) build-test $(FS_DIR) *.z64
.PHONY: clean test

ifneq ($(wildcard $(BUILD_DIR)),)
  -include $(shell find $(BUILD_DIR) -name '*.d')
endif
