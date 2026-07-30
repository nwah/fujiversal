#ifndef FUJI_CALL_H
#define FUJI_CALL_H

#include <stdint.h>

typedef struct {
  uint8_t device;
  uint8_t command;
  uint8_t aux_descr;
  uint8_t aux[4];
  void *buffer;
  uint16_t length;
} FujiNetParams;

extern uint8_t __FASTCALL__ fujiF5_write(FujiNetParams *params);
extern uint8_t __FASTCALL__ fujiF5_read(FujiNetParams *params);

/* The same two calls reached through UNAPI_ENTRY, so that code inside this
   ROM takes the same path as any external UNAPI client. See unapi_call.s. */
extern uint8_t __FASTCALL__ unapi_fuji_write(FujiNetParams *params);
extern uint8_t __FASTCALL__ unapi_fuji_read(FujiNetParams *params);

#endif /* FUJI_CALL_H */
