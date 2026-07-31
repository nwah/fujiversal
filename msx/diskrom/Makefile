# FujiNet MSX Disk ROM
#
# A self-contained 16K disk ROM: the MSX Disk BIOS from apoloval/msx-system
# with a FujiNet driver linked in. Unlike the UNAPI ROM this one carries its
# own MSX-DOS 1 / Disk BASIC kernel, so it hands out drive letters on its own,
# and unlike Nextor it needs no ROM mapper -- which keeps MSX1 in scope.
#
# bios/diskbios.asm is vendored from msx-system and mechanically converted
# from pasmo to z88dk syntax by tools/pasmo2z88dk.py. It stands in as the
# ROM's custom header so that it lands at 4000h and the driver links after it.

CC       = zcc
CFLAGS   = +msx -O3 --opt-code-size -subtype=rom -pragma-define:CRT_MSX_CUSTOM_HEADER=1
BUILD    = build
ROM      = $(BUILD)/fujidisk.rom

CSRC     = src/main.c src/disk.c src/fuji_call.c
# driver.asm is not listed here: the BIOS INCLUDEs it at its tail
ASRC     = src/port_getc.s src/port_getc_timeout.s src/port_getbuf_slip_dual.s \
           src/port_putbuf_slip.s src/port_putc.s src/timeout.s
DEPS     = $(CSRC) $(ASRC) src/driver.asm bios/diskbios.asm src/header/msx_rom_header.asm

all: $(ROM)

$(ROM): $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -Ca-Isrc/header -Ca-Isrc -create-app $(CSRC) $(ASRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) src/*.o

.PHONY: all clean
