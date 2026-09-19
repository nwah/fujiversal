#include "setup_sm.h"
#include "board_defs.h"
#include "FujiBusPacket.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include "fujiROMType.h"
#include <cstddef>
#include <cstdint>

// RD_PIN boards only: without /RD we can't tell a bus read from a write, so
// we can't decode the subslot-select write at FFFFh.
#ifdef RD_PIN
#define UNAPI_ROM_PATH STR(build/BOARD_NAME/unapi_rom.h)
#include UNAPI_ROM_PATH
#endif

#define VERBOSE_DEBUG 0

#ifdef USE_STDIO
#include <stdio.h>
#define DEBUG_PRINTF printf
#else
#pragma GCC poison printf putchar getchar
#endif // USE_STDIO

#include <array>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <hardware/pio.h>
#include <hardware/irq.h>
#include <hardware/watchdog.h>
#include <hardware/clocks.h>
#include <tusb.h>

#include <string>

#ifdef RW_PIN
#define IO_TOP     (IO_BASE + 2)
#else
#define IO_TOP     (IO_BASE + 4)
#endif

#define IO_FLAG_USERROM_READY	    0x40
#define IO_FLAG_ROM_MODE_CMD		  0b00000100
#define IO_FLAG_USERROM_ENABLE	  0b00000001
#define IO_FLAG_AUTOSTART_ENABLE	0b00000010
#define IO_FLAG_UNAPI_ENABLE		  0b00010000
#define IO_FLAG_EXPAND_SLOT		  0b00100000
#define IO_FLAG_ROM_BANK_CMD		  0b10000000
#define IO_MASK_ROM_BANK			    0b00001111

#define ROM_SEG_SIZE 16384
#ifdef PICO_RP2040
#define ROM_MAX_SEGS 8
#else
#define ROM_MAX_SEGS 16
#endif // PICO_RP2040
#define SIZE_8K   0x2000
#define SIZE_16K  0x4000

#define USE_IRQ 0

#define PSM_WAITSEL 0
#define PSM_READ    1
#if !defined(BOARD_picorom_coco) && !defined(BOARD_picorom_msx) && !defined(BOARD_msxrp2350)
#define PSM_SENDBUS 2
#endif

pio_sm_t state_machine[3];
#define pio_get_fifo(n) pio_sm_get_blocking(state_machine[n].pio, state_machine[n].sm)
#define pio_put_fifo(n, d) pio_sm_put(state_machine[n].pio, state_machine[n].sm, d)

#define POW2_CEIL(x_) ({      \
    unsigned int x = x_; \
    x -= 1;              \
    x = x | (x >> 1);    \
    x = x | (x >> 2);    \
    x = x | (x >> 4);    \
    x = x | (x >> 8);    \
    x = x | (x >>16);    \
    x + 1; })

#define RING_SIZE 1024
#define ring_append(buf, in, x) ({buf[in] = x; in = (in + 1) % sizeof(buf); })
// Drops the byte if the ring is full: core1 can't be stalled, and waiting
// here would starve the watchdog.
#define check_tx() ({ \
      if (multicore_fifo_rvalid()) {                    \
        bus.combined = multicore_fifo_pop_blocking();   \
        tx_put(bus.data);                               \
      }                                                 \
    })


uint8_t user_rom[ROM_MAX_SEGS * ROM_SEG_SIZE];
uint8_t * volatile user_rom_base = nullptr;
volatile uint8_t user_rom_selected_bank = 0;
volatile bool user_rom_closed = false;
volatile bool user_rom_active = false;

// One of the images the cartridge can serve: built-in ROM, UNAPI, or the
// streamed user ROM. Each has its own base/mapper/pages.
struct rom_image {
  const uint8_t *base;
  uint32_t       size;
  fujiROMType_t  type;
  volatile uint16_t bank_size;
  volatile uint16_t bank_count;
  // Derived once in set_image_type(), not per read: bank_size is volatile,
  // so re-reading it costs a reload each use.
  uint8_t        bank_shift; // offset >> this == bank index
  uint16_t       bank_mask;  // offset & this == offset within the bank
  uint8_t        pages;      // bit N set => answers in page N
  uint32_t       bank_offsets[4];
};

rom_image boot_image = { nullptr, 0, ROM_TYPE_UNKNOWN, SIZE_16K, 1, 14, SIZE_16K - 1, 0b0110, {0, 0, 0, 0} };
rom_image user_image = { nullptr, 0, ROM_TYPE_UNKNOWN, SIZE_16K, 1, 14, SIZE_16K - 1, 0b0110, {0, 0, 0, 0} };

#ifdef RD_PIN
// Page 1 only (16K at 4000h): an empty page 2 is what lets the IO window
// live there instead of being shadowed.
rom_image unapi_image = { nullptr, 0, ROM_TYPE_UNKNOWN, SIZE_16K, 1, 14, SIZE_16K - 1, 0b0010, {0, 0, 0, 0} };

// Starts true: at power-on the slot is expanded with UNAPI in subslot 0 and
// the built-in ROM in subslot 1 (IO_CONTROL mode $3C).
volatile bool unapi_enabled = true;

// Expanded means we own FFFFh: written, it selects a subslot per page; read,
// it returns the complement -- how the BIOS detects an expanded slot.
volatile bool expanded = true;
volatile uint8_t subslot_reg = 0;
#endif // RD_PIN

#ifdef BOARD_coco_proto_260402
// Power-on-like Program Pak boot: on user-ROM enable we point rom_ptr at the
// .CCC (so $C000 != "DK" and the reset routine can't reload HDB-DOS), pulse
// RESET_PIN low for a clean hardware reset, then toggle CART_PIN at ~60Hz like
// a real Program Pak's CART line so the reset routine autostarts the cartridge.
volatile bool reset_active = false;          // RESET asserted, pending release
volatile uint32_t reset_assert_ms = 0;       // when core 0 asserted RESET
volatile bool cart_toggle_active = false;    // core 0 toggles CART_PIN at ~60Hz
volatile uint32_t cart_toggle_start_ms = 0;  // when the CART toggle started
#define RESET_PULSE_MS 50                   // hold RESET low this long
#define CART_TOGGLE_MS 1500                 // toggle CART this long after autostart
#endif // BOARD_coco_proto_260402

#define SERIAL_BEGIN_DELAY 100

#ifndef USE_STDIO
#include <stdarg.h>
#include <stdio.h>
#include "tusb.h"

#define DEBUG_PRINTF tusb_printf

void tusb_printf(const char *format, ...)
{
  char buf[256];
  va_list args;


  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (len) {
    tud_cdc_write(buf, (uint32_t)len);
    tud_cdc_write_flush();
    while (tud_cdc_write_available() < CFG_TUD_CDC_TX_BUFSIZE)
      tud_task();
  }

  return;
}
#endif // ! USE_STDIO

#if USE_IRQ
bool selected = 0;

void __isr pio_irq_handler()
{
  if (pio_interrupt_get(pio0, 0)) {
    selected = 1;
    // Clear the PIO IRQ flag
    pio_interrupt_clear(pio0, 0);
  }
}
#endif

void setup_pio_irq_logic()
{
  pio_sm_config conf;
  uint offset;


  // Init all GPIO pins to inputs with no pulls
  for (uint pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
  }

  // Setup state machine that checks when we are selected
  {
    sm_setup_t waitsel_setup {};
    waitsel_init_setup(&waitsel_setup);
    setup_state_machine(&state_machine[PSM_WAITSEL], &waitsel_setup);
  }

#ifdef PSM_SENDBUS
  // Setup state machine that sends addr/data bus signals
  {
    sm_setup_t send_bus_setup {};
    send_bus_init_setup(&send_bus_setup);
    setup_state_machine(&state_machine[PSM_SENDBUS], &send_bus_setup);
  }
#endif // PSM_SENDBUS

  // Setup state machine that handles CPU read by putting byte on bus
  {
    sm_setup_t read_setup {};
    read_init_setup(&read_setup);
    setup_state_machine(&state_machine[PSM_READ], &read_setup);
  }

#ifdef BOARD_coco_proto_260402
  // FIXME - doesn't belong here
  gpio_pull_up(IGNORE_PIN); // unused middle pin needs to be inverted to avoid false zero

  // Park CART_PIN high (deasserted) at boot; the autostart toggle drives it.
  gpio_init(CART_PIN);
  gpio_set_dir(CART_PIN, GPIO_OUT);
  gpio_put(CART_PIN, 1);

  // RESET_PIN is open-drain: latch 0 so driving it OUT asserts RESET low, and
  // start as input (released - the CoCo's pull-up holds RESET high).
  gpio_init(RESET_PIN);
  gpio_put(RESET_PIN, 0);
  gpio_set_dir(RESET_PIN, GPIO_IN);
#endif

#if USE_IRQ
  pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
  irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
  irq_set_enabled(PIO0_IRQ_0, true);
#endif

  return;
}

// Plain multiplication, not the bank_count wrap a guest's own bank-register
// write gets below: bank_count is still 1 here (before any bytes arrive),
// and that wrap would flatten every window onto block 0.
void reset_image_banks(struct rom_image *img)
{
  for (unsigned n = 0; n < 4; n++)
    img->bank_offsets[n] = (uint32_t)n * img->bank_size;
}

void set_image_type(struct rom_image *img, fujiROMType_t t)
{
  img->type = t;
  img->bank_size = (t & 0x80) ? SIZE_16K : SIZE_8K;
  img->bank_shift = (t & 0x80) ? 14 : 13;
  img->bank_mask = img->bank_size - 1;
  img->bank_count = (uint16_t)((img->size + img->bank_size - 1) / img->bank_size);
  if (img->bank_count == 0)
    img->bank_count = 1;
  reset_image_banks(img);
}

// File-scope so refresh_mapping() can update it between cycles instead of
// romulan() re-deriving it every cycle.
uint8_t *rom_ptr = disk_rom;

// Which image answers in each page, and whether the IO window is decoded.
// Recomputed only when something changes, never per cycle: a read has to
// land inside the Z80's timing window, and rebuilding this from several
// volatiles every cycle is what blew that budget before.
#ifdef RD_PIN
struct rom_image * volatile page_image[4] = { nullptr, nullptr, nullptr, nullptr };
#endif // RD_PIN
volatile bool io_window_live = true;

// Set on either core when the mapping needs rebuilding; the bus loop picks
// it up between cycles, never mid-cycle. Starts true so the mapping is built
// before the first cycle.
volatile bool mapping_dirty = true;

// __time_critical_func: called from romulan() in RAM. In flash it's reached
// through a veneer into XIP, and a miss there -- likely, since core0 runs
// TinyUSB from the same cache during a push -- can cost more than the
// ~500ns read deadline, dropping that cycle and the ones behind it.
void __time_critical_func(refresh_mapping)(void)
{
#ifdef RD_PIN
  for (unsigned page = 0; page < 4; page++) {
    struct rom_image *img;

    if (expanded) {
      // UNAPI in subslot 0; subslot 1 is the built-in ROM until a user ROM
      // is enabled. BIOS walks subslots in order, so UNAPI's INIT runs first.
      unsigned ss = (subslot_reg >> (2 * page)) & 3;
      img = (ss == 0) ? &unapi_image
          : (ss == 1) ? (user_rom_active ? &user_image : &boot_image)
          : nullptr;
    } else {
      img = unapi_enabled   ? &unapi_image
          : user_rom_active ? &user_image
          : &boot_image;
    }

    // No buffer, or wrong page: treat as unmapped so the read path only has
    // to test the pointer.
    if (img && (!img->base || !(img->pages & (1u << page))))
      img = nullptr;
    page_image[page] = img;
  }

  // Nothing wants the user ROM any more: fall back to the built-in one.
  if (!user_rom_base && rom_ptr != disk_rom)
    rom_ptr = disk_rom;

  // Expanded: window lives in subslot 0 (UNAPI's), so subslot 1 gets a clean
  // 4000h-BFFFh. Unexpanded: over the built-in ROM as always, and never for
  // a user ROM (a cartridge, which owns the whole window).
  io_window_live = expanded ? (((subslot_reg >> 4) & 3) == 0)
                            : !user_rom_active;
#else
  if (!user_rom_base && rom_ptr != disk_rom)
    rom_ptr = disk_rom;
  io_window_live = !user_rom_active;
#endif // RD_PIN
}

#ifdef RD_PIN
// Always answers exactly one byte (0xFF for empty): the read SM stalls
// holding the bus if it gets none.
//
// always_inline: a veneer call into RAM would cost more than the lookup.
static inline __attribute__((always_inline))
void serve_rom_read(uint16_t addr)
{
  struct rom_image *img = page_image[addr >> 14];
  uint32_t rom_offset, idx;
  uint8_t bank;

  if (!img) {
    pio_put_fifo(PSM_READ, 0xFF);
    return;
  }

  rom_offset = addr;

  if (img->type == ROM_TYPE_MSX_KONAMI) {
    // [0x0000, 0x4000) mirrors [0x4000, 0x8000)
    if (addr < 0x4000) rom_offset += 0x4000;
    // [0xC000, 0x10000) mirrors [0x8000, 0xC000)
    else if (addr >= 0xC000) rom_offset -= 0x4000;
  }
  else if (img->type == ROM_TYPE_MSX_KONAMI_SCC) {
    // [0x0000, 0x4000) mirrors [0xC000, 0x10000)
    if (addr < 0x4000) rom_offset += 0x8000;
    // [0xC000, 0x10000) mirrors [0x4000, 0x8000)
    else if (addr >= 0xC000) rom_offset -= 0x8000;
  }

  rom_offset -= BUS_ROM_BASE;
  bank = rom_offset >> img->bank_shift;

  if (bank >= 4) {
    // Guest-controlled bank register can compute past this hardware's
    // window; fail soft instead of indexing off the end.
    pio_put_fifo(PSM_READ, 0xFF);
    return;
  }

  idx = img->bank_offsets[bank] + (rom_offset & img->bank_mask);
  if (idx >= img->size) {
    // Past the image end -- reachable via a bank register or a short image
    // on its own. 0xFF rather than whatever the buffer last held, which the
    // BIOS's page-2 scan could otherwise mistake for a header.
    pio_put_fifo(PSM_READ, 0xFF);
    return;
  }

  pio_put_fifo(PSM_READ, img->base[idx]);
}
#endif // RD_PIN

void __time_critical_func(romulan)(void)
{
  BusSignals bus;
#ifndef RD_PIN
  uint32_t rom_offset;
#endif // RD_PIN
  uint32_t last_bus_state = -1;
  uint8_t bank = 0;
  bool switch_bank = false;

  setup_pio_irq_logic();

  while (true) {
    // Applied here, between cycles, never mid-cycle: refresh_mapping() costs
    // ~100 instructions, more than the read budget, so running it inside a
    // cycle made that cycle's byte late (wrong opcode / stale GETC / missed
    // status bit, depending which cycle it landed on). A mapping-changing
    // write (IO_CONTROL, FFFFh) loops back here immediately, so the next
    // fetch already sees it; a flag core0 raises is picked up one cycle
    // later, which is safe since it never changes what an in-flight cycle
    // is being served from.
    if (mapping_dirty) {
      mapping_dirty = false;
      refresh_mapping();
    }

#ifdef PSM_SENDBUS
    bus.combined = pio_get_fifo(PSM_SENDBUS);
#else
    bus.combined = pio_get_fifo(PSM_WAITSEL);
#endif // PSM_SENDBUS
    // This board's PIO can't re-sample an access (wait_sel fires once per
    // /SLTSL and re-arms only after it's released), so every FIFO entry is a
    // distinct cycle the Z80 is waiting on -- dropping one here would answer
    // a real read with nothing. coco_proto_260402 has the same handshake.
#if !defined(BOARD_picorom_coco) && !defined(BOARD_coco_proto_260402) && !defined(BOARD_msx_proto_260402)
    if (bus.combined == last_bus_state)
      continue;
#endif

#if 0
    DEBUG_PRINTF("ADDR:%04x DATA:%02x CTS:%d SCS:%d RW:%d UN:%d COMBINED:0x%08x\r\n",
                 bus.addr, bus.data, 0, bus.scs, bus.rw, bus.unused, bus.combined);
#endif

#ifdef RD_PIN
    // Fast lane: reads of 4000h-BFFFh (including the IO window) are nearly
    // every cycle that matters, with a window only ~0.5us wide, so answer
    // before the rarer FFFFh/bank-register checks rather than after them.
    if (!bus.rd && BUS_ROM_BASE <= bus.addr && bus.addr < BUS_ROM_TOP) {
      if (io_window_live && bus.addr >= IO_BASE) {
        // PUTC/CONTROL are write-only; answer 0xFF rather than fall through
        // (the read SM must always get a byte).
        unsigned io_reg = bus.addr & 0x3;

        if (io_reg == IO_GETC)
          pio_put_fifo(PSM_READ, sio_hw->fifo_rd);
        else if (io_reg == IO_STATUS)
          pio_put_fifo(PSM_READ,
            (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS ? IO_FLAG_AVAIL : 0x00)
            | (user_rom_closed ? IO_FLAG_USERROM_READY : 0x00)
          );
        else
          pio_put_fifo(PSM_READ, 0xFF);
      }
      else
        serve_rom_read(bus.addr);

      last_bus_state = bus.combined;
      continue;
    }
#endif // RD_PIN

    // FIXME - only check IO_BASE if rom_ptr == disk_rom
    if (io_window_live && IO_BASE <= bus.addr && bus.addr < IO_TOP) {
      unsigned io_reg = (bus.addr - IO_BASE) & 0x3;
#ifdef RW_PIN
      if (!bus.rw)
        io_reg |= 2;
#endif // RW_PIN
#ifdef RD_PIN
      // Reads here were already answered above, so this is a write. No R/W
      // line on this board, so nothing else tells GETC/STATUS's read from a
      // write -- without this, writing either would run the read case and
      // leave the read SM one byte ahead for the rest of the session.
      if (io_reg != IO_PUTC && io_reg != IO_CONTROL)
        io_reg = ~0u; // matches no case below
#endif // RD_PIN

      switch (io_reg) {
      case IO_GETC: // Read byte
        pio_put_fifo(PSM_READ, sio_hw->fifo_rd);
        break;
      case IO_STATUS: // Read status reg
        pio_put_fifo(PSM_READ,
          (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS ? IO_FLAG_AVAIL : 0x00)
          | (user_rom_closed ? IO_FLAG_USERROM_READY : 0x00)
        );
        break;
      case IO_PUTC: // Write byte
        sio_hw->fifo_wr = bus.combined;
        break;

      case IO_CONTROL: // Write control reg
        // Any assignment below can change the mapping; flag once here
        // rather than after each one.
        mapping_dirty = true;
      	if (bus.data & IO_FLAG_ROM_MODE_CMD) {
#ifdef RD_PIN
          unapi_enabled = (bus.data & IO_FLAG_UNAPI_ENABLE) != 0;
          // Un/expanding invalidates any selected subslot: the BIOS zeroes
          // this on its slot scan, but that hasn't run yet when this write
          // lands.
          {
            bool want_expanded = (bus.data & IO_FLAG_EXPAND_SLOT) != 0;
            if (want_expanded != expanded)
              subslot_reg = 0;
            expanded = want_expanded;
          }
#endif // RD_PIN
          // UNAPI is additive: boards that can't serve it just ignore these
          // two flags and do the rest, since CONFIG can't know which board
          // it's on. $3D degrades to plain user ROM.
          bool want_user = (bus.data & IO_FLAG_USERROM_ENABLE) != 0;

          // Enable/disable user ROM
          if (want_user) {
            user_rom_active = true;
            rom_ptr = &user_rom[user_rom_selected_bank * ROM_SEG_SIZE];
            user_image.base = rom_ptr;
            if (bus.data & IO_FLAG_AUTOSTART_ENABLE) {
#ifdef BOARD_coco_proto_260402
              // Enable auto start (CoCo)
              // Start the ~60Hz CART toggle and pulse RESET low;
              // the CoCo comes out of a clean hardware reset and its own reset
              // routine autostarts the cartridge.
              cart_toggle_active = true;
              cart_toggle_start_ms = to_ms_since_boot(get_absolute_time());
              gpio_put(RESET_PIN, 0);
              gpio_set_dir(RESET_PIN, GPIO_OUT);   // assert RESET low
              reset_active = true;
              reset_assert_ms = to_ms_since_boot(get_absolute_time());
#endif // BOARD_coco_proto_260402
            }
          }
          else {
            // Drop the ready flag too: a second request could otherwise see
            // the previous load's flag and boot a still-incomplete buffer.
            user_rom_active = false;
            user_rom_closed = false;
            rom_ptr = &disk_rom[0];
          }
       	}
        else if (bus.data & IO_FLAG_ROM_BANK_CMD) {
          user_rom_selected_bank = (bus.data & IO_MASK_ROM_BANK) & (ROM_MAX_SEGS - 1);
          rom_ptr = &user_rom[user_rom_selected_bank * ROM_SEG_SIZE];
          user_image.base = rom_ptr;
        }
        break;
      }
    }
#ifdef RD_PIN
    // PIO fires on /SLTSL with no address mask, so FFFFh reaches here
    // whenever we're selected in page 3.
    else if (expanded && bus.addr == 0xFFFF) {
      if (!bus.rd) { // /RD asserted: this is a read
        pio_put_fifo(PSM_READ, (uint8_t)~subslot_reg);
      } else {
        subslot_reg = bus.data;
        // Flagged, not rebuilt here: with the slot expanded, every
        // inter-slot call (60Hz interrupt, disk access) writes this
        // register, and the Z80's next cycles are fetches -- rebuilding now
        // would stall exactly where it can't afford to.
        mapping_dirty = true;
      }
    }
    else if (bus.rd && (0x4000 <= bus.addr) && (bus.addr < 0xC000)) {
      // Belongs to whichever image is mapped where it was written -- not
      // necessarily what's running, since UNAPI banks from page 1 while a
      // cartridge sits in another subslot's page 2.
      struct rom_image *img = page_image[bus.addr >> 14];
      if (img) {
        switch_bank = false;
        switch (img->type) {
          case ROM_TYPE_MSX_ASCII8:
            if ((0x6000 <= bus.addr) && (bus.addr < 0x8000)) {
              // 0x6000 = 0, 0x6800 = 1, 0x7000 = 2, 0x7800 = 3
              bank = (bus.addr >> 11) & 3;
              switch_bank = true;
            }
            break;
          case ROM_TYPE_MSX_ASCII16:
            if ((0x6000 <= bus.addr) && (bus.addr < 0x7800) && !(bus.addr & 0x0800)) {
              // 0x6000 = 0, 0x7000 = 1
              bank = (bus.addr >> 12) & 1;
              switch_bank = true;
            }
            break;
          case ROM_TYPE_MSX_KONAMI:
            // [0x4000..0x6000) is fixed at segment 0.
            if (0x6000 <= bus.addr && bus.addr < 0xC000) {
              // 0x6000 = 3, 0x8000 = 4, 0xA000 = 5
              // subtract 2 because ROM starts at 0x4000
              bank = (bus.addr >> 13) - 2;
              switch_bank = true;
            }
            break;
          case ROM_TYPE_MSX_KONAMI_SCC:
            if (0x5000 <= bus.addr && bus.addr < 0xC000 && (bus.addr & 0x1800) == 0x1000) {
              // 0x5000 = 2, 0x7000 = 3, 0x9000 = 4, 0xB000 = 5
              // subtract 2 because ROM starts at 0x4000
              bank = (bus.addr >> 13) - 2;
              switch_bank = true;
              // TODO: if bank = 4 clear SCC cache
            }
            break;
          default:
            break;
        }
        if (switch_bank)
          img->bank_offsets[bank] = (bus.data % img->bank_count) * img->bank_size;
      }
    }
#endif
    else if ((BUS_ROM_BASE <= bus.addr && bus.addr < BUS_ROM_TOP)) {
#ifndef RD_PIN
      // No banking hardware: serve directly to keep the response path short
      rom_offset = bus.addr - BUS_ROM_BASE;
      bus.data = rom_ptr[rom_offset];
      pio_put_fifo(PSM_READ, bus.data);
#else
      // Everything the fast path above did not take: a read of the IO window
      // while it is switched off.
      serve_rom_read(bus.addr);
#endif // RD_PIN
    }

    last_bus_state = bus.combined;
  }

  return;
}

// Replies share this ring with IO_PUTC bytes so a reply can't be sent in the
// middle of a partly drained frame.
unsigned char ring_tx[RING_SIZE];
unsigned ring_tx_in = 0, ring_tx_out = 0;

void tx_drain(void)
{
  if (ring_tx_in == ring_tx_out)
    return;

#ifdef USE_STDIO
  putchar(ring_tx[ring_tx_out]);
  ring_tx_out = (ring_tx_out + 1) % sizeof(ring_tx);
#else
  unsigned contig = (ring_tx_in > ring_tx_out)
    ? (ring_tx_in - ring_tx_out)
    : (sizeof(ring_tx) - ring_tx_out);

  while (tud_cdc_write_available() < 1)
    tud_task();

  uint32_t room = tud_cdc_write_available();
  uint32_t to_write = (contig < room) ? contig : room;
  uint32_t written = tud_cdc_write(&ring_tx[ring_tx_out], to_write);
  tud_cdc_write_flush();
  ring_tx_out = (ring_tx_out + written) % sizeof(ring_tx);
#endif // USE_STDIO
}

bool tx_put(uint8_t c)
{
  unsigned next = (ring_tx_in + 1) % sizeof(ring_tx);

  if (next == ring_tx_out)
    return false;

  ring_tx[ring_tx_in] = c;
  ring_tx_in = next;
  return true;
}

// Waits for room rather than dropping: a truncated reply desyncs the link.
void tx_enqueue(const uint8_t *data, size_t length)
{
  while (length--) {
    while (!tx_put(*data)) {
      tx_drain();
      tud_task();
    }
    data++;
  }
}

void sendReplyPacket(fujiDeviceID_t source, bool ack, void *data, size_t length)
{
    FujiBusPacket packet(source, ack ? FUJICMD_ACK : FUJICMD_NAK,
                         ack ? std::string(static_cast<const char*>(data), length) : "");
    ByteBuffer encoded = packet.serialize();
#if VERBOSE_DEBUG
    DEBUG_PRINTF("Sending reply: dev:%02x cmd:%02x len:%04x\n",
           packet.device(), packet.command(), encoded.size());
#endif // VERBOSE_DEBUG
    tx_enqueue(encoded.data(), encoded.size());
#if VERBOSE_DEBUG
    DEBUG_PRINTF("Sent\n");
#endif // VERBOSE_DEBUG
    return;
}

#define DBC_STREAM_ROM 0

bool process_command(ByteBuffer &buffer)
{
  static int user_rom_write_pos = -1;
  auto packet = FujiBusPacket::fromSerialized(buffer);


  if (!packet) {
#if VERBOSE_DEBUG
    DEBUG_PRINTF("Failed to decode packet\n");
#endif // VERBOSE_DEBUG
    return false;
  }

  switch (packet->command()) {
  case FUJICMD_OPEN:
    {
      auto reply = [&](bool ok) { sendReplyPacket(packet->device(), ok, nullptr, 0); };

      uint8_t stream_id;
      uint32_t declared;
      fujiROMType_t rt;

      if (packet->data() && packet->data()->size() >= 6) {
        // stream_id, size (32-bit little-endian), rom_type
        const ByteBuffer &hdr = *packet->data();
        stream_id = hdr[0];
        declared  = (uint32_t)hdr[1] | ((uint32_t)hdr[2] << 8) |
                    ((uint32_t)hdr[3] << 16) | ((uint32_t)hdr[4] << 24);
        rt = (fujiROMType_t)hdr[5];
      } else if (packet->paramCount() >= 1) {
        // Older FujiNets and the CoCo send params instead of the header.
        stream_id = DBC_STREAM_ROM;
        declared  = 0;
        rt = (packet->paramCount() >= 2) ? (fujiROMType_t)packet->param(1) : ROM_TYPE_UNKNOWN;
      } else {
        reply(false);
        break;
      }

      if (rt == ROM_TYPE_UNKNOWN) {
        // Senders with no mapper concept send 0, which would pick 8K banks.
        rt = ROM_TYPE_MSX_PLAIN;
      }

      if (stream_id != DBC_STREAM_ROM) {
        reply(false);
        break;
      }

      if (declared != 0 && declared > sizeof(user_rom)) {
        // 0 means the size is unknown, not empty.
        reply(false);
        break;
      }

      user_rom_base      = user_rom;
      user_rom_write_pos = 0;
      user_rom_closed    = false;
      // Before set_image_type(), or the previous image's bank count survives.
      user_image.size = 0;
      set_image_type(&user_image, rt);
      mapping_dirty = true;
      reply(true);
#if VERBOSE_DEBUG
      DEBUG_PRINTF("Opening ROM: stream %d type 0x%02x declared %lu\n",
                    stream_id, rt, (unsigned long)declared);
#endif // VERBOSE_DEBUG
    }
    break;

  case FUJICMD_WRITE:
    {
      if (user_rom_write_pos < 0 || !user_rom_base || !packet->data()) {
        sendReplyPacket(packet->device(), false, nullptr, 0);
        break;
      }

      // Measured from user_rom_base in case OPEN ever sets a non-zero one.
      size_t base_offset = (size_t)(user_rom_base - user_rom);
      size_t used = base_offset + (size_t)user_rom_write_pos;
      size_t avail = used < sizeof(user_rom) ? sizeof(user_rom) - used : 0;
      if (packet->data()->size() > avail) {
        sendReplyPacket(packet->device(), false, nullptr, 0);
        break;
      }

      size_t len = packet->data()->size();
#if VERBOSE_DEBUG
      DEBUG_PRINTF("Writing %d bytes to 0x%04x\n", len, user_rom_write_pos);
#endif // VERBOSE_DEBUG
      if (len) {
        memcpy(&user_rom_base[user_rom_write_pos], packet->data()->data(), len);
        user_rom_write_pos += len;
      }

      sendReplyPacket(packet->device(), true, nullptr, 0);
    }
    break;

  case FUJICMD_CLOSE:
    {
      // Payload 0x01 aborts: a partial image must not be marked bootable.
      bool aborted = packet->data() && !packet->data()->empty() &&
                     (*packet->data())[0] == 0x01;

      if (user_rom_write_pos < 0 || !user_rom_base) {
        sendReplyPacket(packet->device(), false, nullptr, 0);
        break;
      }

      if (aborted) {
        user_rom_write_pos  = -1;
        user_rom_base       = nullptr;
        user_rom_closed     = false;
        user_image.bank_count = 1;
        reset_image_banks(&user_image);
        mapping_dirty = true;
        sendReplyPacket(packet->device(), true, nullptr, 0);
        break;
      }

      // From what was written: the declared size may be 0 (unknown).
      user_image.size = (uint32_t)user_rom_write_pos;
      if (user_rom_write_pos > 0)
        user_image.bank_count = (uint16_t)((user_rom_write_pos + user_image.bank_size - 1) / user_image.bank_size);
      if (user_image.bank_count == 0)
        user_image.bank_count = 1;
      mapping_dirty = true;
      user_rom_write_pos = -1;
      user_rom_closed = true;
#if VERBOSE_DEBUG
      DEBUG_PRINTF("Closing RAM %d\n", user_rom_closed);
#endif // VERBOSE_DEBUG
      sendReplyPacket(packet->device(), true, nullptr, 0);
    }
    break;

  case FUJICMD_RESET:
    // Sends no reply, so the FujiNet must not wait for an ACK.
    user_rom_write_pos = -1;
    user_rom_base = nullptr;
    user_rom_active = false;
    user_rom_closed = false;
    user_rom_selected_bank = 0;
    user_image.bank_count = 1;
    mapping_dirty = true;
    break;

  default:
    // NAK so the FujiNet doesn't sit out its 500ms read timeout.
    sendReplyPacket(packet->device(), false, nullptr, 0);
    break;
  }

  return true;
}

int main()
{
  BusSignals bus;
  int input;
  unsigned int count = 0;
  unsigned char ring_rx[RING_SIZE];
  unsigned ring_rx_in = 0, ring_rx_out = 0;
  uint32_t last_cc_seen = 0, last_ring_sent = 0, now, loop_begin;
  bool our_command = false, serial_ready = false;
  ByteBuffer command_buf;
  enum { RX_IDLE, RX_DECIDING, RX_MSX_FRAME, RX_DBC_FRAME } rx_state = RX_IDLE;

  // user_image gets its type at FUJICMD_OPEN and its base in IO_CONTROL.
  boot_image.base = disk_rom;
  boot_image.size = sizeof(disk_rom);
  set_image_type(&boot_image, ROM_TYPE_MSX_PLAIN);
#ifdef RD_PIN
  unapi_image.base = unapi_rom;
  unapi_image.size = sizeof(unapi_rom);
  set_image_type(&unapi_image, ROM_TYPE_MSX_PLAIN);
#endif // RD_PIN
  refresh_mapping();

  set_sys_clock_khz(250000, true);

#ifdef LED_PIN
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
#endif // LED_PIN

  multicore_launch_core1(romulan);

#ifdef USE_STDIO
  stdio_init_all();
  stdio_set_translate_crlf(&stdio_usb, false);

  while (!stdio_usb_connected())
    ;
#else
  tusb_init();
  while (!tud_cdc_connected()) {
    tud_task();
    check_tx();
  }
#endif // USE_STDIO

#if 0
  if (watchdog_caused_reboot())
    printf("Watchdog rebooted!\r\n");
#endif

  watchdog_enable(100, 1);

  loop_begin = to_ms_since_boot(get_absolute_time());
  while (true) {
    watchdog_update();
    now = to_ms_since_boot(get_absolute_time());

    if (!serial_ready && now - loop_begin > SERIAL_BEGIN_DELAY)
      serial_ready = true;

#ifdef BOARD_coco_proto_260402
    // Release RESET once the pulse has elapsed (open-drain: back to input) so
    // the CoCo boots from a clean hardware reset. Unsigned delta is
    // wraparound-safe.
    if (reset_active && now - reset_assert_ms >= RESET_PULSE_MS) {
      gpio_set_dir(RESET_PIN, GPIO_IN);
      reset_active = false;
    }
    // Toggle CART at ~60Hz (period ~16ms) like a Program Pak's CART line so the
    // reset routine's cart-FIRQ check fires and autostarts the .CCC. Stop after
    // the autostart window and park CART high (deasserted) so leftover CART
    // FIRQs don't disrupt a later CFGLOAD/CONFIG.BIN boot. Unsigned delta is
    // wraparound-safe.
    if (cart_toggle_active) {
      if (now - cart_toggle_start_ms >= CART_TOGGLE_MS) {
        cart_toggle_active = false;
        gpio_put(CART_PIN, 1);
      }
      else
        gpio_put(CART_PIN, (now >> 3) & 1);
    }
#endif // BOARD_coco_proto_260402

    check_tx();

    // A frame addressed to us that never finished: hand the bytes to the
    // host rather than sit on them forever.
    if (rx_state == RX_DBC_FRAME && now - last_cc_seen > 50) {
#if VERBOSE_DEBUG
      DEBUG_PRINTF("Command timeout %d\r\n", command_buf.size());
#endif // VERBOSE_DEBUG
      for (char c : command_buf)
        ring_append(ring_rx, ring_rx_in, (uint8_t) c);
      command_buf.clear();
      rx_state = RX_IDLE;
    }

    tud_task();
    if ((ring_rx_in + 1) % RING_SIZE != ring_rx_out || now - last_ring_sent > 10) {
#ifdef LED_PIN
      //gpio_put(LED_PIN, 0);
#endif
      input = tud_cdc_available();
      if (input > 0) {
        unsigned char rc;
        tud_cdc_read(&rc, 1);
        input = rc;
        // One byte of lookahead past a leading SLIP_END says whose frame
        // this is (every frame is SLIP_END, body, SLIP_END, and the second
        // byte names the device) -- so only that leading byte is ever held
        // back.
        switch (rx_state) {
        case RX_IDLE:
          if (input == SLIP_END)
            rx_state = RX_DECIDING;
          else
            ring_append(ring_rx, ring_rx_in, (uint8_t) input);
          break;

        case RX_DECIDING:
          if (input == SLIP_END) {
            // Empty frame, or a run of terminators: give the host the one
            // we were holding and keep waiting on this one.
            ring_append(ring_rx, ring_rx_in, (uint8_t) SLIP_END);
          }
          else if (input == FUJI_DEVICEID_DBC) {
            command_buf.clear();
            command_buf.push_back((char) SLIP_END);
            command_buf.push_back((char) input);
            last_cc_seen = to_ms_since_boot(get_absolute_time());
            rx_state = RX_DBC_FRAME;
          }
          else {
            ring_append(ring_rx, ring_rx_in, (uint8_t) SLIP_END);
            ring_append(ring_rx, ring_rx_in, (uint8_t) input);
            rx_state = RX_MSX_FRAME;
          }
          break;

        case RX_MSX_FRAME:
          ring_append(ring_rx, ring_rx_in, (uint8_t) input);
          if (input == SLIP_END)
            rx_state = RX_IDLE;
          break;

        case RX_DBC_FRAME:
          command_buf.push_back((char) input);
          last_cc_seen = to_ms_since_boot(get_absolute_time());
          if (input == SLIP_END) {
            if (!process_command(command_buf)) {
              for (char c : command_buf)
                ring_append(ring_rx, ring_rx_in, (uint8_t) c);
            }
            command_buf.clear();
            rx_state = RX_IDLE;
          }
          break;
        }
      }
    }
    else {
      //printf("RING FULL\r\n");
#ifdef LED_PIN
      //gpio_put(LED_PIN, 1);
#endif
    }

    if (ring_rx_in != ring_rx_out) {
      bool sent = multicore_fifo_push_timeout_us(ring_rx[ring_rx_out], 0);
      if (sent) {
        ring_rx_out = (ring_rx_out + 1) % sizeof(ring_rx);
        last_ring_sent = now;
      }
    }

    if (serial_ready)
      tx_drain();
  }

  return 0;
}
