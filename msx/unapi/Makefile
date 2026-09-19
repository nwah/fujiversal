# ROM_HEADER = header.s
# ROM_AFILES =
# ROM_CFILES = dispatch.c
# CFLAGS = +z80 --no-crt -c
# LDFLAGS = -m=z80 -b -r=0x4000
# BINFILE = header_code_user.bin

# NORM_AFILES := $(ROM_AFILES:.asm=.s)
# OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(ROM_CFILES)) \
#         $(patsubst %.s,$(OBJ_DIR)/%.o,$(NORM_AFILES))

# unapi.rom: $(BINFILE)
# 	mv $^ $@
# 	ls -Fla unapi*
# 	hexdump -C $@

# $(BINFILE): $(ROM_HEADER) $(OBJS)
# 	z80asm $(LDFLAGS) $^

# %.o: %.c
# 	zcc $(CFLAGS) $^ -o $@

PRODUCT = unapi
PLATFORMS += msxrom

# You can run 'make <platform>' to build for a specific platform,
# or 'make <platform>/<target>' for a platform-specific target.
# Example shortcuts:
#   make coco        → build for coco
#   make apple2/disk → build the 'disk' target for apple2

# SRC_DIRS may use the literal %PLATFORM% token.
# It expands to the chosen PLATFORM plus any of its combos.
SRC_DIRS = src src/%PLATFORM%

# FUJINET_LIB is not used

# Define extra dirs ("combos") that expand with a platform.
# Format: platform+=combo1,combo2
PLATFORM_COMBOS = \
  c64+=commodore \
  atarixe+=atari \
  msxrom+=msx \
  msxdos+=msx \
  adam_cpm+=adam

# Pinned and fetched, like msx/bootstrap's $(FNLIB) rule. Ref resolved to a
# commit before checkout: a branch only exists as origin/<name> in a fresh
# clone, and checkout --detach won't accept that directly. Moved into place
# only once checkout succeeds, so a half-finished clone can't pass for a
# good one later.
MEKKOGX_URL ?= https://github.com/FozzTexx/MekkoGX.git
MEKKOGX_REF ?= c9442e0
MEKKOGX     ?= _cache/mekkogx-$(MEKKOGX_REF)

# Target must be the literal included path, not just the checkout dir --
# that's what lets make's "remake a missing included makefile" pass find
# this rule on a fresh checkout. Because of that same restart, this rule
# would become the default goal (make's default goal is the first target
# in the file) on a build starting from nothing, ahead of mekkogx's own
# `all`; .DEFAULT_GOAL below puts that back.
#
# Patches one line afterward: upstream's msxrom.mk sets MSXROM_FLAGS
# unconditionally, which would stomp the -subtype=callext this ROM needs
# (entered as a UNAPI call, not run at boot). `?=` lets our own setting
# below win instead.
$(MEKKOGX)/mekkogx/toplevel-rules.mk:
	mkdir -p $(dir $(MEKKOGX))
	rm -rf $(MEKKOGX).tmp
	git clone $(MEKKOGX_URL) $(MEKKOGX).tmp
	git -C $(MEKKOGX).tmp checkout --detach \
	  $$(git -C $(MEKKOGX).tmp rev-parse --verify --quiet '$(MEKKOGX_REF)^{commit}' \
	     || git -C $(MEKKOGX).tmp rev-parse --verify 'origin/$(MEKKOGX_REF)^{commit}')
	git -C $(MEKKOGX).tmp apply $(CURDIR)/patches/mekkogx-msxrom-flags-override.patch
	mv $(MEKKOGX).tmp $(MEKKOGX)

.DEFAULT_GOAL := all

include $(MEKKOGX)/mekkogx/toplevel-rules.mk

# If you need to add extra platform-specific steps, do it below:
#   coco/r2r:: coco/custom-step1
#   coco/r2r:: coco/custom-step2
# or
#   apple2/disk: apple2/custom-step1 apple2/custom-step2

MSXROM_FLAGS = +msx -subtype=callext
MSX_EXTRA_DEPS = src/header/msx_rom_header.asm
LDFLAGS_EXTRA_MSXROM = -Ca-Isrc/header -create-app
