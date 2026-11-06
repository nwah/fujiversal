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

// Timeouts scale by this. Fixed at 60Hz, deliberately: this used to be
//
//   #define VDP_IS_PAL (((unsigned char *) 0x002b) & 0x80)
//
// which masks the *address*, not the byte there, so it folded to constant 0
// (always NTSC). A correct dereference would be worse: that byte is the
// main BIOS ROM's, and under MSX-DOS/Nextor -- where this ROM lives
// resident -- page 0 is RAM, not the BIOS, so it'd read garbage. timeout.s
// counts 60Hz-calibrated loop iterations rather than frame interrupts now,
// so nothing here depends on the actual VDP standard.
#define VDP_IS_PAL 0
