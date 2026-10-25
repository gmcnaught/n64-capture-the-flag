ROOT := ../..
BUILD_DIR := build
BIN := $(BUILD_DIR)/ctf
.DEFAULT_GOAL := all

CORE_SRCS := $(wildcard $(ROOT)/core/*.c)
HOST_SRCS := main.c audio.c
COMMON_SRCS := replay.c scene.c audio_names.c cpu.c
XM_SRCS := xm.c load.c play.c analyze.c
OBJS := $(HOST_SRCS:%.c=$(BUILD_DIR)/platform/host/%.o) $(CORE_SRCS:$(ROOT)/%.c=$(BUILD_DIR)/%.o) \
        $(COMMON_SRCS:%.c=$(BUILD_DIR)/platform/common/%.o) $(XM_SRCS:%.c=$(BUILD_DIR)/libxm/%.o)

SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -I$(ROOT)/core -I$(ROOT)/platform/common $(SDL_CFLAGS) -MMD -MP
CFLAGS += -DTUNING_HEADER_PATH='"$(abspath $(ROOT)/core/player_tuning.h)"'
CFLAGS += -I$(BUILD_DIR)/libxm

# libxm (external/libxm/README): the options its CMakeLists would define.
# Music loops forever; the sample rate is set at runtime (audio.c).
XM_DIR := external/libxm
# external/compat supplies <stdbit.h> where the SDK has none (macOS).
XM_CFLAGS := -O2 -g -std=c2x -I$(BUILD_DIR)/libxm -idirafter external/compat -DXM_VERBOSE=0 -DXM_LINEAR_INTERPOLATION=1 \
             -DXM_RAMPING=1 -DXM_LIBXM_DELTA_SAMPLES=1 -DXM_STRINGS=1 -DXM_TIMING_FUNCTIONS=0 \
             -DXM_MUTING_FUNCTIONS=0 -DXM_SAMPLE_RATE=0 -DXM_MICROSTEP_BITS=12 \
             -DXM_PANNING_TYPE=8 -DXM_LOOPING_TYPE=0 -DXM_DISABLED_EFFECTS=0ULL \
             -DXM_DISABLED_VOLUME_EFFECTS=0ULL -DXM_DISABLED_FEATURES=0ULL -MMD -MP

ifeq ($(shell uname),Darwin)
  XM_CFLAGS += -D_Float32=float     # Apple clang has no C23 _Float32 (IEEE binary32 = float)
endif

$(BUILD_DIR)/libxm/xm.h: $(XM_DIR)/xm.h.in
	@mkdir -p $(dir $@)
	sed 's/@XM_SAMPLE_TYPE@/int16_t/' $< > $@

$(BUILD_DIR)/libxm/%.o: $(XM_DIR)/%.c $(BUILD_DIR)/libxm/xm.h
	$(CC) $(XM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/platform/host/audio.o: $(BUILD_DIR)/libxm/xm.h

ROUTEFIND := $(BUILD_DIR)/routefind
NAVBUILD := $(BUILD_DIR)/navbuild
all: $(BIN) $(ROUTEFIND) $(NAVBUILD)

# CPU players' navigation graphs (the top Makefile's assets).
navbuild: $(NAVBUILD)
$(NAVBUILD): $(ROOT)/tools/navbuild.c $(CORE_SRCS:$(ROOT)/%.c=$(BUILD_DIR)/%.o)
	$(CC) -O2 -std=c99 -Wall -Wextra -I$(ROOT)/core -o $@ $^

# Route finder for scripting replays (tools/make_replays.py).
$(ROUTEFIND): $(ROOT)/tools/routefind.c $(CORE_SRCS:$(ROOT)/%.c=$(BUILD_DIR)/%.o) \
              $(BUILD_DIR)/platform/common/replay.o
	$(CC) -O2 -std=c99 -Wall -Wextra -I$(ROOT)/core -I$(ROOT)/platform/common -o $@ $^

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(SDL_LIBS)

$(BUILD_DIR)/platform/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/core/%.o: $(ROOT)/core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/platform/common/%.o: $(ROOT)/platform/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	$(BIN)

selftest: $(BIN)
	$(BIN) --selftest

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run selftest clean

-include $(OBJS:.o=.d)
