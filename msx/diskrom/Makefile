# FujiNet MSX Disk ROM
#
# A self-contained 32K cartridge, 4000h-BFFFh:
#
#   page 1  4000h-7FFFh  MSX Disk BIOS + the FujiNet disk driver
#   page 2  8000h-BFFFh  UNAPI implementation + the FujiNet transport
#
# Unlike the UNAPI ROM this one carries its own MSX-DOS 1 / Disk BASIC kernel,
# so it hands out drive letters on its own, and unlike Nextor it needs no ROM
# mapper -- which keeps MSX1 in scope.
#
# bios/diskbios.asm is vendored from msx-system and mechanically converted
# from pasmo to z88dk syntax by tools/pasmo2z88dk.py. It stands in as the
# ROM's custom header so that it lands at 4000h and the driver links after it.
#
# Page 2 is the BANK_01 section, orged at 8000h by src/page2.s. appmake writes
# that section at ROM offset 4000h, which is where a 32K cartridge's page 2
# begins, and pads the image to 32K. Assembly modules name the section
# themselves; the C in page 2 is compiled separately because the section has
# to be given to the compiler rather than written in the source.
#
# That separate compilation uses sdcc rather than sccz80, and it has to.
# sccz80 leans on a handful of tiny helper routines -- l_gint, l_ne and
# friends -- which come out of the library into the CODE section, which is
# page 1. Page 2 cannot call page 1: when a UNAPI client enters at the entry
# point in page 2, page 1 is whatever RAM the client was using. sdcc emits
# code with no such dependencies, so the page 2 object stands alone.
#
# The two compilers do not agree on calling conventions, so nothing in page 1
# calls into page 2 in C: the only way through is the UNAPI entry point,
# which is assembly on both sides. See unapi_call.s.

CC       = zcc
CFLAGS   = +msx -O3 --opt-code-size -subtype=rom -pragma-define:CRT_MSX_CUSTOM_HEADER=1
# --opt-code-size is deliberately absent: it makes sdcc reach for the shared
# prologue helpers (__sdcc_enter_ix and friends), which come out of the
# library into page 1 and would be a call page 2 cannot make.
CFLAGS_P2 = +msx -compiler=sdcc -SO3 --max-allocs-per-node200000 -subtype=rom
INCS     = -Ca-Isrc/header -Ca-Isrc
BUILD    = build
ROM      = $(BUILD)/fujidisk.rom

# The last four bytes of page 2 are the FujiNet IO window. Nothing may be
# placed there, so the page 2 section has to stop short of them.
P2_MAX   = 16380

# Page 1 has to stop short of 8000h, and nothing in the toolchain says so:
# appmake only complains past 32K, and page 2 is written over the overflow
# afterwards, so the ROM comes out looking fine and the tail of page 1 is
# quietly gone. check_pages.py reads the link map and refuses that.
CHECK    = tools/check_pages.py

# Page 1: the driver, and the door through to page 2
CSRC_P1  = src/main.c src/disk.c
# driver.asm is not listed here: the BIOS INCLUDEs it at its tail
ASRC_P1  = src/unapi_call.s src/unapi_init.s

# Page 2: the UNAPI implementation and the FujiNet transport
CSRC_P2  = src/fuji_call.c
ASRC_P2  = src/page2.s src/const.s src/unapi_entry.s src/do_extbio.s \
           src/getslt_p2.s src/getwrk.s src/toupper.s \
           src/port_getc.s src/port_getc_timeout.s src/port_getbuf_slip_dual.s \
           src/port_putbuf_slip.s src/port_putc.s src/timeout.s

OBJ_P2   = $(patsubst src/%.c,$(BUILD)/%.o,$(CSRC_P2))

HDRS     = $(wildcard src/*.h src/*.inc)
DEPS     = $(CSRC_P1) $(ASRC_P1) $(ASRC_P2) $(HDRS) \
           src/driver.asm bios/diskbios.asm src/header/msx_rom_header.asm

all: $(ROM)

$(ROM): $(DEPS) $(OBJ_P2) $(CHECK) | $(BUILD)
	$(CC) $(CFLAGS) $(INCS) -create-app -m \
	    $(CSRC_P1) $(ASRC_P1) $(ASRC_P2) $(OBJ_P2) -o $@
	@python3 $(CHECK) $(BUILD)/fujidisk.map $(BUILD)/fujidisk_BANK_01.bin $(P2_MAX)

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS_P2) $(INCS) --codeseg=BANK_01 --constseg=BANK_01 -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) src/*.o

.PHONY: all clean
