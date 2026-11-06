BOARD ?= picorom_msx

ifneq ($(filter $(BOARD),picorom_coco coco_proto_260402),)
  ROM_IMAGE = hdbdw3bc3.rom
else
  ROM_IMAGE = config-msx.rom
endif

# Boards with /RD can decode FFFFh, so they also serve the UNAPI ROM in a subslot.
ifneq ($(shell grep -c RD_PIN boards/$(BOARD).pio),0)
  UNAPI_ROM = unapi-msx.rom
  UNAPI_H = $(BUILD_DIR)/unapi_rom.h
endif

BUILD_DIR = build/$(BOARD)
BUILD_MAKE = $(BUILD_DIR)/Makefile
FIRMWARE = fujiversal_$(BOARD).uf2

ROM_H = $(BUILD_DIR)/rom.h
UF2_BINARY = $(BUILD_DIR)/fujiversal_$(BOARD).uf2

SRC = main.cpp board_defs.h setup_sm.cpp setup_sm.h FujiBusPacket.cpp	\
      FujiBusPacket.h UserROM.h UserROM.cpp Mapper.h fujiDeviceID.h	\
      fujiCommandID.h diag_uart.cpp diag_uart.h $(ROM_H) $(UNAPI_H)

$(BUILD_DIR)/$(FIRMWARE): $(SRC) $(BUILD_MAKE)
	defoogi make -C $(BUILD_DIR)

$(BUILD_MAKE): CMakeLists.txt boards/$(BOARD).pio
	defoogi cmake -B $(BUILD_DIR) -DBOARD=$(BOARD)

upload: $(BUILD_DIR)/$(FIRMWARE)
	defoogi sudo picotool load -v -x $(UF2_BINARY) -f

picorom_msx picorom_coco msxrp2350 msx_proto_260402 coco_proto_260402:
	$(MAKE) BOARD=$@

all: $(BOARD)

clean:
	rm -rf build

# Don't leave a truncated target behind when a recipe fails
.DELETE_ON_ERROR:

$(ROM_H): $(ROM_IMAGE) | $(BUILD_DIR)
	defoogi xxd -i -n disk_rom $< > $@

$(UNAPI_H): $(UNAPI_ROM) | $(BUILD_DIR)
	defoogi xxd -i -n unapi_rom $< > $@

# Rebuilds the committed unapi-msx.rom from msx/unapi.
unapi-rom:
	AS_DEFAULT=z88dk-z80asm defoogi make -C msx/unapi
	cp msx/unapi/r2r/msxrom/unapi.rom unapi-msx.rom

.PHONY: unapi-rom

$(BUILD_DIR):
	mkdir -p $@
