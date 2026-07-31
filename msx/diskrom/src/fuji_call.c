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

/* The JIFFY counter the timeout routines read ticks once per video frame:
   60 times a second on an NTSC machine, 50 on a PAL one. These are worked out
   for 60Hz, so on a PAL machine every timeout runs a fifth longer, which for
   waiting on a bus reply is no difference worth a runtime division. */
#define JIFFIES_PER_SECOND      60
#define milliseconds_to_jiffy(millis) ((millis) * JIFFIES_PER_SECOND / 1000)
#define seconds_to_jiffy(secs)  ((secs) * JIFFIES_PER_SECOND)

#define TIMEOUT         milliseconds_to_jiffy(100)
#define TIMEOUT_SLOW	seconds_to_jiffy(15)

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

static uint16_t fuji_calc_checksum(const void *ptr, uint16_t len, uint16_t seed)
{
  uint16_t idx, chk;
  uint8_t *buf = (uint8_t *) ptr;


  for (idx = 0, chk = seed; idx < len; idx++)
    chk = ((chk + buf[idx]) >> 8) + ((chk + buf[idx]) & 0xFF);
  return chk;
}

static uint8_t fuji_packet_call(AtariSIODirection direction,
                                fujibus_packet *packet_ptr,
                                void *pbuf, uint16_t plen)
{
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

  // The IO window is simply there: this code is in page 2 and cannot be
  // running unless page 2 holds the cartridge. pbuf is the caller's, and the
  // UNAPI specification forbids it from being in page 1 or page 2, so it is
  // visible throughout.
  port_putc(SLIP_END);
  port_putbuf_slip(packet_ptr, aux_len + sizeof(packet_ptr->header));
  if (direction == SIO_DIRECTION_WRITE)
    port_putbuf_slip(pbuf, plen);
  port_putc(SLIP_END);

  if (direction != SIO_DIRECTION_READ) {
    pbuf = NULL;
    plen = 0;
  }
  rlen = port_getbuf_slip_dual(packet_ptr, sizeof(packet_ptr->header),
                               pbuf, plen, TIMEOUT_SLOW);
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
  return success;
}

/*
 * The two UNAPI routines themselves: unpack a FujiNetParams into a packet and
 * make the call. FN_TABLE in const.s reaches these, and so does the disk
 * driver in page 1 -- through the UNAPI entry point, like anyone else.
 *
 * The packet is a local, so it lives on the caller's stack. That is safe for
 * the same reason the call works at all: a caller whose stack were in page 2
 * would lose its own return address the moment this ROM was paged in.
 */
static uint8_t fuji_unapi_call(AtariSIODirection direction, FujiNetParams *params)
{
  uint8_t idx, numbytes;
  fujibus_packet fb_packet;


  fb_packet.header.device = params->device;
  fb_packet.header.command = params->command;
  fb_packet.header.fields = params->aux_descr;

  numbytes = fuji_field_numbytes(params->aux_descr);
  for (idx = 0; idx < numbytes; idx++)
    fb_packet.data[idx] = params->aux[idx];

  return fuji_packet_call(direction, &fb_packet, params->buffer, params->length);
}

uint8_t fujiF5_write(FujiNetParams *params) __z88dk_fastcall
{
  return fuji_unapi_call(SIO_DIRECTION_WRITE, params);
}

uint8_t fujiF5_read(FujiNetParams *params) __z88dk_fastcall
{
  return fuji_unapi_call(SIO_DIRECTION_READ, params);
}
