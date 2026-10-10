# libretro core for beebjit. Expects the beebjit tree at ./beebjit (git submodule).
# Cross-build example (see ../build-beebjit.sh):
#   make CC="zig cc -target aarch64-linux-gnu.2.31" CFLAGS="-O3 -DNDEBUG -mcpu=cortex_a53"
BEEBJIT ?= ./beebjit
CC ?= cc
TARGET := beebjit_libretro.so

CFLAGS ?= -O3 -DNDEBUG
override CFLAGS += -fPIC -Wall -Wno-unknown-warning-option -Wno-address-of-packed-member \
          -I$(BEEBJIT) -I$(BEEBJIT)/asm -I. $(PLATFORM_HDR)
ASFLAGS = -fPIC -I$(BEEBJIT) -I$(BEEBJIT)/asm -I. $(PLATFORM_HDR)
# asm_platform_libretro.h replaces beebjit's fixed-address platform header; see
# the comment in that file.
PLATFORM_HDR := -DBEEBJIT_ASM_PLATFORM_H -include asm_platform_libretro.h
LDFLAGS += -shared -Wl,--no-undefined -Wl,-version-script=link.T -lm -lpthread

BEEBJIT_C := \
  config.c bbc.c defs_6502.c state.c video.c via.c \
  emit_6502.c interp.c inturbo.c state_6502.c sound.c timing.c \
  jit_compiler.c jit_metadata.c cpu_driver.c \
  jit_optimizer.c jit_opcode.c keyboard.c \
  teletext.c render.c mc6850.c serial_ula.c \
  log.c test.c adc.c cmos.c joystick.c \
  tape.c tape_csw.c tape_uef.c \
  intel_fdc.c wd_fdc.c \
  disc_drive.c disc.c ibm_disc_format.c disc_tool.c \
  disc_fsd.c disc_hfe.c disc_ssd.c disc_adl.c \
  disc_rfi.c disc_kryo.c disc_scp.c disc_dfi.c \
  debug.c expression.c jit.c \
  util.c util_string.c util_container.c util_compress.c \
  asm/asm_abi.c asm/asm_tables.c asm/asm_util.c \
  asm/asm_common.c asm/asm_inturbo.c asm/asm_jit.c

BEEBJIT_S := asm/asm_common.S asm/asm_inturbo.S asm/asm_jit.S

LOCAL_C := core.c os_libretro.c

OBJDIR := obj
OBJS := $(addprefix $(OBJDIR)/, $(BEEBJIT_C:.c=.o) $(BEEBJIT_S:.S=.S.o) $(LOCAL_C:.c=.o))

PYTHON ?= python3

all: $(TARGET)

roms_data.h: gen_roms.py $(BEEBJIT)/roms/os12.rom $(BEEBJIT)/roms/basic.rom
	$(PYTHON) gen_roms.py $(BEEBJIT) $@

$(OBJDIR)/core.o: roms_data.h

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJDIR)/%.o: $(BEEBJIT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.S.o: $(BEEBJIT)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET) roms_data.h

.PHONY: all clean
