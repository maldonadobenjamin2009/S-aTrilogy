# Makefile (Linux / macOS / MSYS2-MinGW) - alternativa sencilla a CMake.
#   make            -> bin/SonicAdvanceTrilogy   (necesita SDL2: sdl2-config o pkg-config)
#   make test       -> bin/headless_test         (prueba sin ventana, con el mGBA real)
MGBA   ?= third_party/mgba
BIN    ?= bin
CC     ?= gcc

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  PLATFORM := windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  PLATFORM := windows
else ifeq ($(UNAME_S),Darwin)
  PLATFORM := mac
else
  PLATFORM := linux
endif

CFLAGS ?= -O2
MGBA_FLAGS := -std=gnu11 -w -I$(MGBA)/include -I$(MGBA)/src \
  -DM_CORE_GBA -DENABLE_VFS -DENABLE_DIRECTORIES -DENABLE_VFS_FD -DMINIMAL_CORE=2 \
  -DMGBA_STANDALONE -DHAVE_STRDUP
ifeq ($(PLATFORM),windows)
  MGBA_FLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
  OS_SRC := $(sort $(wildcard $(MGBA)/src/platform/windows/*.c))
  LDLIBS := -lm -lws2_32 -lshlwapi
  EXE := .exe
else ifeq ($(PLATFORM),mac)
  MGBA_FLAGS += -DHAVE_LOCALTIME_R -D_DARWIN_C_SOURCE -DHAVE_LOCALE -DHAVE_XLOCALE -DHAVE_SNPRINTF_L -DHAVE_STRTOF_L
  OS_SRC := $(wildcard $(MGBA)/src/platform/posix/*.c) $(MGBA)/src/util/vfs/vfs-dirent.c
  MGBA_FLAGS += -DUSE_PTHREADS
  LDLIBS := -lm -pthread
else
  MGBA_FLAGS += -D_GNU_SOURCE -DHAVE_LOCALE -DHAVE_LOCALTIME_R -DUSE_PTHREADS
  OS_SRC := $(wildcard $(MGBA)/src/platform/posix/*.c) $(MGBA)/src/util/vfs/vfs-dirent.c
  LDLIBS := -lm -pthread
endif

ARM_SRC  := arm.c decoder-arm.c decoder.c decoder-thumb.c isa-arm.c isa-thumb.c
CORE_SRC := bitmap-cache.c cache-set.c cheats.c config.c core.c directories.c input.c interface.c lockstep.c \
            log.c map-cache.c mem-search.c rewind.c serialize.c sync.c thread.c tile-cache.c timing.c
GBA_SRC  := audio.c bios.c cart/ereader.c cart/gpio.c cart/matrix.c cart/unlicensed.c cart/vfame.c cheats.c \
            cheats/codebreaker.c cheats/gameshark.c cheats/parv3.c core.c dma.c gba.c hle-bios.c input.c io.c \
            memory.c overrides.c renderers/cache-set.c renderers/common.c renderers/gl.c renderers/software-bg.c \
            renderers/software-mode0.c renderers/software-obj.c renderers/video-software.c savedata.c serialize.c \
            sharkport.c sio.c sio/gbp.c sio/dolphin.c sio/lockstep.c timer.c video.c
UTIL_SRC := circle-buffer.c configuration.c crc32.c formatting.c gbk-table.c hash.c md5.c sha1.c string.c table.c \
            vector.c vfs.c audio-buffer.c audio-resampler.c convolve.c elf-read.c geometry.c image.c image/export.c \
            image/font.c image/png-io.c interpolator.c patch.c patch-fast.c patch-ips.c patch-ups.c ring-fifo.c \
            sfo.c text-codec.c vfs/vfs-mem.c vfs/vfs-fifo.c vfs/vfs-fd.c

MGBA_SRCS := $(addprefix $(MGBA)/src/arm/,$(ARM_SRC)) $(addprefix $(MGBA)/src/core/,$(CORE_SRC)) \
  $(addprefix $(MGBA)/src/gba/,$(GBA_SRC)) $(MGBA)/src/gb/audio.c $(addprefix $(MGBA)/src/util/,$(UTIL_SRC)) \
  $(MGBA)/src/third-party/inih/ini.c $(MGBA)/src/feature/thread-proxy.c $(MGBA)/src/feature/video-logger.c $(OS_SRC) third_party/version.c
APP_SRCS  := src/gfx.c src/png.c src/font_data.c src/sys.c src/config.c src/emu.c src/app_logic.c src/app_ui.c

OBJDIR := build
MGBA_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(MGBA_SRCS))
APP_OBJS  := $(patsubst %.c,$(OBJDIR)/%.o,$(APP_SRCS))

$(OBJDIR)/$(MGBA)/%.o: $(MGBA)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MGBA_FLAGS) -c $< -o $@
$(OBJDIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MGBA_FLAGS) -c $< -o $@
$(OBJDIR)/src/emu.o: src/emu.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MGBA_FLAGS) -c $< -o $@
$(OBJDIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Isrc -c $< -o $@

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)

all: $(BIN)/SonicAdvanceTrilogy$(EXE)
$(BIN)/SonicAdvanceTrilogy$(EXE): $(APP_OBJS) $(MGBA_OBJS) $(OBJDIR)/src/main_sdl.o
	@mkdir -p $(BIN)
	$(CC) -o $@ $^ $(SDL_LIBS) $(LDLIBS)
$(OBJDIR)/src/main_sdl.o: src/main_sdl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Isrc $(SDL_CFLAGS) -c $< -o $@

test: $(BIN)/headless_test$(EXE)
$(BIN)/headless_test$(EXE): tests/headless.c $(APP_OBJS) $(MGBA_OBJS)
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Isrc -o $@ $^ $(LDLIBS)

clean:
	rm -rf $(OBJDIR) $(BIN)
.PHONY: all test clean
