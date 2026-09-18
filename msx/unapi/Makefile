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

# Pinned rather than vendored, the same way msx/bootstrap fetches fujinet-lib
# (see that Makefile's own $(FNLIB) rule -- same idiom, mirrored here).
#
# The ref is resolved to a commit before checkout, because a bare name means
# different things depending on what it names: a commit is in the clone under
# that name, while a branch is only there as origin/<name>, and handing that
# name straight to "checkout --detach" makes git read it as a request for a
# local branch and refuse.
#
# The clone lands beside the target and is moved into place only once the
# checkout has worked. A directory left behind by a half-finished clone would
# satisfy the -d test on a later run and quietly build against whatever branch
# the clone happened to leave checked out.
MEKKOGX_URL ?= https://github.com/FozzTexx/MekkoGX.git
MEKKOGX_REF ?= c9442e0
MEKKOGX     ?= _cache/mekkogx-$(MEKKOGX_REF)

# The rule's target has to be the literal path being included, not just the
# checkout directory: that is what lets make's own "remake an out-of-date
# included makefile" pass find this rule before the include line below is
# ever reached, on a totally fresh checkout where neither exists yet.
#
# Because of that same restart mechanism, this rule becomes the default goal
# (make's default goal is the first target in the file) on a build starting
# from nothing, ahead of mekkogx's own `all` -- hence .DEFAULT_GOAL below.
#
# One local patch goes on afterward: upstream's msxrom.mk sets MSXROM_FLAGS
# unconditionally, which would stomp the -subtype=callext this project needs
# (a UNAPI ROM is entered as a call, not run at boot, so it cannot be the
# self-booting -subtype=rom the platform defaults to) with whatever platform
# default happens to be linked in last. The patch changes that one line to
# `?=`, so the project's own setting below wins and upstream's is only a
# fallback for a caller that never set one.
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
