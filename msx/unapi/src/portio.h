#include <stdint.h>

// returns signed int with data or -1 if no data is available
extern int __FASTCALL__ port_getc();

// return data if it arrives before timeout or -1 if timeout expires
extern int __FASTCALL__ port_getc_timeout(uint16_t timeout);

// The two values these routines write to the primary slot register: the low
// byte pages the caller's RAM into page 2, the high byte pages in this ROM so
// the IO window is reachable. They swap between the two around every access
// to buf, so a buffer sitting in page 2 stays visible during a transfer.
#define PORT_SLOTS(ram, io)     ((uint16_t) (ram) | ((uint16_t) (io) << 8))

// reads and decodes SLIP into two buffers
// returns length of data received, if timeout expires returns all data received until then
// timeout resets when a character is received
extern uint16_t __CALLEE__ port_getbuf_slip_dual(void *hdr_buf, uint16_t hdr_len,
                                                 void *data_buf, uint16_t data_len,
                                                 uint16_t timeout, uint16_t slots);

// writes character to port
extern void __FASTCALL__ port_putc(uint8_t c);

// writes data to port handling SLIP escapes, returns number of bytes written
extern uint16_t __CALLEE__ port_putbuf_slip(const void *buf, uint16_t len,
                                            uint16_t slots);

#define VDP_IS_PAL (((unsigned char *) 0x002b) & 0x80)
