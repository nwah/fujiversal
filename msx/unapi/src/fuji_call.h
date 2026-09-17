#ifndef FUJI_CALL_H
#define FUJI_CALL_H

#include <stdint.h>
#include <stdbool.h>

/* Aligns with the fujinet-lib-experimental "unapi" branch header
 * bus/msx/fujinet-unapi-msx.h.  The ROM defines fuji_unapi_call() itself:
 * an inter-slot call can enter this ROM but cannot page its bank register,
 * so the library's CALSLT back end is useless here. */

#define FUJI_UNAPI_ID "FUJINET"

typedef struct {
  uint8_t device;
  uint8_t command;
  uint8_t aux_descr;
  uint8_t aux1, aux2, aux3, aux4;
  void *buffer;
  uint16_t length;
} FujiNetParams;

/* Routine numbers from the FujiNet Firmware UNAPI specification 1.0,
 * which assigns nothing to 1. */
enum {
  FUJI_CALL_INFO  = 0,
  FUJI_CALL_WRITE = 2,
  FUJI_CALL_READ  = 3,
};

/* Transport: called by fuji_bus_call and by the FN_TABLE entry points.
 * Built into the ROM, so it talks directly to the SLIP/IO-window transport
 * rather than going through the library's EXTBIO/CALSLT back end. */
extern uint8_t fuji_unapi_call(uint8_t func, FujiNetParams *params);

/* Entry points published in the FN_TABLE (const.s). */
extern uint8_t __FASTCALL__ fujiF5_write(FujiNetParams *params);
extern uint8_t __FASTCALL__ fujiF5_read(FujiNetParams *params);

/* Slot/subslot helpers (subslot.s).
 * io_window_enter() returns the subslot register as it found it, which the
 * caller passes back to io_window_exit; there is no separate "was it
 * changed" flag, as both routines re-test EXPTBL for themselves. */
extern uint8_t io_window_enter(void);
extern void __FASTCALL__ io_window_exit(uint8_t saved);

#endif /* FUJI_CALL_H */
