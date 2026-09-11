#include <stdint.h>

// returns signed int with data or -1 if no data is available
extern int __FASTCALL__ port_getc();

// return data if it arrives before timeout or -1 if timeout expires
extern int __FASTCALL__ port_getc_timeout(uint16_t timeout);

// reads and decodes SLIP into two buffers
// returns length of data received, if timeout expires returns all data received until then
// timeout resets when a character is received
extern uint16_t __CALLEE__ port_getbuf_slip_dual(void *hdr_buf, uint16_t hdr_len,
                                                 void *data_buf, uint16_t data_len,
                                                 uint16_t timeout);

// writes character to port
extern void __FASTCALL__ port_putc(uint8_t c);

// writes data to port handling SLIP escapes, returns number of bytes written
extern uint16_t __CALLEE__ port_putbuf_slip(const void *buf, uint16_t len);

// The video standard, which callers use to scale a timeout in milliseconds
// into frame times.
//
// Fixed at 60Hz, deliberately. This was written as
//
//   #define VDP_IS_PAL (((unsigned char *) 0x002b) & 0x80)
//
// which masks the *address* 0x2B rather than the byte there, so it folded to
// a constant 0 and every timeout was already computed as NTSC. Adding the
// dereference back would be worse than leaving it: bit 7 of 0x002B is the
// main BIOS ROM's, and page 0 only holds the BIOS when nothing else has been
// switched in -- under MSX-DOS and Nextor, where this ROM is meant to sit
// resident, page 0 is RAM and that byte is whatever happens to be there.
//
// Nothing is lost by pinning it. timeout.s no longer counts frame interrupts
// at all; it counts loop iterations calibrated to a 60Hz frame, so the unit
// on the far side of this divisor is 60Hz-shaped whatever the VDP is doing.
#define VDP_IS_PAL 0
