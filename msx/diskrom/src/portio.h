#include <stdint.h>

/* All of these live in page 2 alongside the IO window they drive, and they
   only ever run with this ROM paged into page 2. Nothing here touches the
   slot register: a buffer handed in can no more be in page 2 than the code
   reading it can, which is exactly what the UNAPI specification requires of
   a caller.

   The conventions are sdcc's, because page 2 is compiled with sdcc -- see
   the Makefile. __z88dk_fastcall passes the single argument in HL and
   returns in HL (or L for a byte); __z88dk_callee pushes arguments right to
   left, so the leftmost ends up nearest SP, and the callee cleans up. */

// returns signed int with data or -1 if no data is available
extern int port_getc(void) __z88dk_fastcall;

// return data if it arrives before timeout or -1 if timeout expires
extern int port_getc_timeout(uint16_t timeout) __z88dk_fastcall;

// reads and decodes SLIP into two buffers
// returns length of data received, if timeout expires returns all data received until then
// timeout resets when a character is received
extern uint16_t port_getbuf_slip_dual(void *hdr_buf, uint16_t hdr_len,
                                      void *data_buf, uint16_t data_len,
                                      uint16_t timeout) __z88dk_callee;

// writes character to port
extern void port_putc(uint8_t c) __z88dk_fastcall;

// writes data to port handling SLIP escapes, returns number of bytes written
extern uint16_t port_putbuf_slip(const void *buf, uint16_t len) __z88dk_callee;
