# OpenOrbis PS4 homebrew build for Sonic Advance Trilogy.
# Run from repository root: make -f ps4/Makefile
# Requires OO_PS4_TOOLCHAIN (OpenOrbis v0.5.3+) and its SDL2 package.

ROOT := .
MGBA := third_party/mgba
SDK := $(OO_PS4_TOOLCHAIN)
BUILD := build-ps4
OBJDIR := $(BUILD)/obj
STAGE := $(BUILD)/stage
TITLE := Sonic Advance Trilogy
VERSION := 1.00
TITLE_ID := BREW00142
CONTENT_ID := IV0000-BREW00142_00-SONICADVANCE0000
PAID := 0x3800000000000011

ARM_SRC := arm.c decoder-arm.c decoder.c decoder-thumb.c isa-arm.c isa-thumb.c
CORE_SRC := bitmap-cache.c cache-set.c cheats.c config.c core.c directories.c input.c interface.c lockstep.c log.c map-cache.c mem-search.c rewind.c serialize.c sync.c thread.c tile-cache.c timing.c
GBA_SRC := audio.c bios.c cart/ereader.c cart/gpio.c cart/matrix.c cart/unlicensed.c cart/vfame.c cheats.c cheats/codebreaker.c cheats/gameshark.c cheats/parv3.c core.c dma.c gba.c hle-bios.c input.c io.c memory.c overrides.c renderers/cache-set.c renderers/common.c renderers/gl.c renderers/software-bg.c renderers/software-mode0.c renderers/software-obj.c renderers/video-software.c savedata.c serialize.c sharkport.c sio.c sio/gbp.c sio/dolphin.c sio/lockstep.c timer.c video.c
UTIL_SRC := circle-buffer.c configuration.c crc32.c formatting.c gbk-table.c hash.c md5.c sha1.c string.c table.c vector.c vfs.c audio-buffer.c audio-resampler.c convolve.c elf-read.c geometry.c image.c image/export.c image/font.c image/png-io.c interpolator.c patch.c patch-fast.c patch-ips.c patch-ups.c ring-fifo.c sfo.c text-codec.c vfs/vfs-mem.c vfs/vfs-fifo.c vfs/vfs-fd.c

MGBA_SRCS := $(addprefix $(MGBA)/src/arm/,$(ARM_SRC)) \
  $(addprefix $(MGBA)/src/core/,$(CORE_SRC)) \
  $(addprefix $(MGBA)/src/gba/,$(GBA_SRC)) \
  $(addprefix $(MGBA)/src/util/,$(UTIL_SRC)) \
  $(MGBA)/src/gb/audio.c $(MGBA)/src/third-party/inih/ini.c \
  $(MGBA)/src/feature/thread-proxy.c $(MGBA)/src/feature/video-logger.c \
  third_party/version.c $(wildcard $(MGBA)/src/platform/posix/*.c) \
  $(MGBA)/src/util/vfs/vfs-dirent.c
APP_SRCS := src/emu.c src/debug_log.c src/ps4_bootstrap.c src/gfx.c src/png.c src/font_data.c src/sys.c src/config.c src/app_logic.c src/app_ui.c src/main_sdl.c
SRCS := $(MGBA_SRCS) $(APP_SRCS)
OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

CC := clang
LD := ld.lld
TOOL_DIR := $(SDK)/bin/linux
PKGTOOL := $(TOOL_DIR)/PkgTool.Core
CREATE_FSELF := $(TOOL_DIR)/create-fself
CREATE_GP4 := $(TOOL_DIR)/create-gp4
SDL_LIB := $(firstword $(wildcard $(SDK)/lib/libSDL2.a $(SDK)/lib/libSDL2.so $(SDK)/lib/libSDL2.dylib))

CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -w -DPS4 -D__ORBIS__ \
  -D_LIBUNWIND_IS_BAREMETAL=1 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
  -DUSE_PTHREADS -DHAVE_LOCALE -DHAVE_LOCALTIME_R -DHAVE_STRTOF_L \
  -DM_CORE_GBA -DENABLE_VFS -DENABLE_DIRECTORIES -DENABLE_VFS_FD -DMINIMAL_CORE=2 -DMGBA_STANDALONE -DHAVE_STRDUP \
  -I$(SDK)/include -I$(SDK)/include/SDL2 -I$(MGBA)/include -I$(MGBA)/src -Isrc
LDFLAGS := -m elf_x86_64 -pie --script $(SDK)/link.x --eh-frame-hdr -L$(SDK)/lib \
  -lc -lkernel -lc++ -lSceUserService -lSceSystemService -lSceVideoOut -lSceAudioOut -lScePad -lSceSysmodule -lSceFreeType -lSDL2 -lm $(SDK)/lib/crt1.o

.PHONY: all clean validate package
all: validate $(BUILD)/SonicAdvanceTrilogy.elf package

validate:
	@test -n "$(SDK)" && test -d "$(SDK)/include" || (echo "ERROR: OO_PS4_TOOLCHAIN no apunta a OpenOrbis"; exit 1)
	@test -f "$(SDK)/lib/libSDL2.a" || test -f "$(SDK)/lib/libSDL2.so" || (echo "ERROR: falta SDL2 de OpenOrbis en $(SDK)/lib"; exit 1)
	@test -x "$(CREATE_FSELF)" -a -x "$(CREATE_GP4)" -a -x "$(PKGTOOL)" || (echo "ERROR: faltan herramientas OpenOrbis en $(TOOL_DIR)"; exit 1)
	@test -d dist/main/roms || (echo "ERROR: falta dist/main/roms"; exit 1)
	@test -d dist/assets || (echo "ERROR: falta dist/assets"; exit 1)
	@find dist/main/roms -type f \( -iname '*.gba' -o -iname '*.gbc' -o -iname '*.gb' \) -print -quit | grep -q . || (echo "ERROR: no se encontraron ROMs .gba/.gbc/.gb rastreadas en Git"; exit 1)
	@echo "SDK: $(SDK)"

$(OBJDIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -isysroot "$(SDK)" -c "$<" -o "$@"

$(BUILD)/SonicAdvanceTrilogy.elf: $(OBJS)
	@mkdir -p "$(BUILD)"
	$(LD) $(OBJS) -o "$@" $(LDFLAGS)

package: $(BUILD)/SonicAdvanceTrilogy.elf
	@bash ps4/package.sh "$(abspath $(BUILD)/SonicAdvanceTrilogy.elf)" "$(abspath $(STAGE))" "$(SDK)" "$(CONTENT_ID)" "$(TITLE_ID)" "$(VERSION)" "$(TITLE)"

clean:
	rm -rf "$(BUILD)"
