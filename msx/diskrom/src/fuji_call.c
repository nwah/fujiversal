#include "fuji_call.h"
#include "portio.h"
#include <string.h>
#include <stdbool.h>

#undef DEBUG
#define HEXDUMP 1

#if defined(DEBUG) && defined(HEXDUMP)
#define COLUMNS 16

static void hexdump(uint8_t *buffer, int count)
{
  int outer, inner;
  uint8_t c;


  for (outer = 0; outer < count; outer += COLUMNS) {
    for (inner = 0; inner < COLUMNS; inner++) {
      if (inner + outer < count) {
	c = buffer[inner + outer];
	printf("%02x ", c);
      }
      else
	printf("   ");
    }
    printf(" |");
    for (inner = 0; inner < COLUMNS && inner + outer < count; inner++) {
      c = buffer[inner + outer];
      if (c >= ' ' && c <= 0x7f)
	printf("%c", c);
      else
	printf(".");
    }
    printf("|\n");
  }

  return;
}
#endif /* HEXDUMP */

#define milliseconds_to_jiffy(millis) ((millis) / (VDP_IS_PAL ? 20 : 1000 / 60))

#define TIMEOUT         milliseconds_to_jiffy(100)
#define TIMEOUT_SLOW	milliseconds_to_jiffy(15 * 1000)

#define false 0
#define true 1

enum {
  PACKET_ACK = 6, // ASCII ACK
  PACKET_NAK = 21, // ASCII NAK
};

enum {
  SLIP_END     = 0xC0,
  SLIP_ESCAPE  = 0xDB,
  SLIP_ESC_END = 0xDC,
  SLIP_ESC_ESC = 0xDD,
};

static const uint8_t fuji_field_numbytes_table[] = {0, 1, 2, 3, 4, 2, 4, 4};
#define fuji_field_numbytes(descr) fuji_field_numbytes_table[descr]

/* The memory mapped IO window lives in page 2, so reaching it means paging
   this ROM's slot over whatever RAM is there. Given the current primary slot
   register this returns the same value with page 2 pointed at the slot page 1
   already holds, which is us: we are executing out of it. */
static uint8_t msx_page2_to_rom(uint8_t slots)
{
  uint8_t my_slot = (slots >> 2) & 0x03;    /* page 1 = this ROM */


  return (slots & 0xCF) | (my_slot << 4);   /* page 2 = the same slot */
}

static uint16_t fuji_calc_checksum(const void *ptr, uint16_t len, uint16_t seed)
{
  uint16_t idx, chk;
  uint8_t *buf = (uint8_t *) ptr;


  for (idx = 0, chk = seed; idx < len; idx++)
    chk = ((chk + buf[idx]) >> 8) + ((chk + buf[idx]) & 0xFF);
  return chk;
}

uint8_t fuji_packet_call(AtariSIODirection direction, fujibus_packet *packet_ptr,
                                void *pbuf, uint16_t plen)
{
  uint8_t slot_ram, slot_io;
  uint16_t slots;
  uint8_t ck1, ck2;
  uint16_t rlen;
  bool success = false;
  uint8_t pdev = packet_ptr->header.device;
  uint8_t aux_len = fuji_field_numbytes(packet_ptr->header.fields);


  packet_ptr->header.length = sizeof(packet_ptr->header) + aux_len;
  if (direction == SIO_DIRECTION_WRITE)
    packet_ptr->header.length += plen;

  // Checksum covers the whole header, so it has to read as zero while
  // being calculated
  packet_ptr->header.checksum = 0;

  // Data is spread across two buffers: packet_ptr and pbuf
  ck1 = fuji_calc_checksum(packet_ptr, aux_len + sizeof(packet_ptr->header), 0);
  if (direction == SIO_DIRECTION_WRITE)
    ck1 = fuji_calc_checksum(pbuf, plen, ck1);
  packet_ptr->header.checksum = ck1;

  // Page in memory mapped IO. pbuf belongs to the caller and may itself be
  // in page 2, so the buffer routines are handed both slot register values
  // and swap back to RAM around each access to it.
  slot_ram = inp(0xA8);
  slot_io = msx_page2_to_rom(slot_ram);
  slots = PORT_SLOTS(slot_ram, slot_io);
  outp(0xA8, slot_io);

  port_putc(SLIP_END);
  port_putbuf_slip(packet_ptr, aux_len + sizeof(packet_ptr->header), slots);
  if (direction == SIO_DIRECTION_WRITE)
    port_putbuf_slip(pbuf, plen, slots);
  port_putc(SLIP_END);

  if (direction != SIO_DIRECTION_READ) {
    pbuf = NULL;
    plen = 0;
  }
  rlen = port_getbuf_slip_dual(packet_ptr, sizeof(packet_ptr->header),
                               pbuf, plen, TIMEOUT_SLOW, slots);
  if (rlen < sizeof(fujibus_header) || rlen != packet_ptr->header.length) {
#ifdef DEBUG
    printf("Reply length incorrect: %d %d\n", rlen, packet_ptr->header.length);
    hexdump((uint8_t *) packet_ptr, sizeof(fujibus_header));
#endif /* DEBUG */
    success = false;
    goto done;
  }
#ifdef DEBUG
  if (rlen - sizeof(fujibus_header) != plen) {
    printf("Expected length incorrect: %d %d\n", rlen - sizeof(fujibus_header), plen);
    hexdump((uint8_t *) packet_ptr, sizeof(fujibus_header));
  }
#endif /* DEBUG */

  // Need to zero out checksum in order to calculate
  ck1 = packet_ptr->header.checksum;
  packet_ptr->header.checksum = 0;

  // Data is spread across two buffers: packet_ptr and pbuf
  ck2 = fuji_calc_checksum(packet_ptr, sizeof(packet_ptr->header), 0);
  if (direction == SIO_DIRECTION_READ)
    ck2 = fuji_calc_checksum(pbuf, rlen - sizeof(packet_ptr->header), ck2);
  ck2 = (uint8_t) ck2;

  if (ck1 != ck2) {
#ifdef DEBUG
    printf("Checksum mismatch: 0x%02x 0x%02x\n", ck1, ck2);
#endif /* DEBUG */
    success = false;
    goto done;
  }

  if (packet_ptr->header.device != pdev) {
#ifdef DEBUG
    printf("Incorrect device: R:0x%02x E:0x%02x\n", packet_ptr->header.device, pdev);
    hexdump((uint8_t *) packet_ptr, sizeof(packet_ptr->header));
#endif /* DEBUG */
    success = false;
    goto done;
  }

  if (packet_ptr->header.command != PACKET_ACK) {
#ifdef DEBUG
    printf("Not ACK: 0x%02x\n", packet_ptr->header.command);
#endif /* DEBUG */
    success = false;
    goto done;
  }

  // FIXME - validate that fb_packet.fields is zero?

  success = true;

 done:
  // Restore whatever was in page 2
  outp(0xA8, slot_ram);
  return success;
}

/* fuji_unapi_call() and fujiF5_read/write() lived here: they unpacked a
   FujiNetParams into the packet above. The driver builds the packet itself
   now, so the wrapper is gone. Both are still in the UNAPI ROM. */
