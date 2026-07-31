#ifndef UNAPI_CALL_H
#define UNAPI_CALL_H

#include "fuji_call.h"

/* The driver's two entry points into page 2, implemented in unapi_call.s.
   Both take a FujiNetParams and return 1 for success, 0 for failure, which
   is what routines 2 and 3 of the FujiNet UNAPI specification say. Neither
   the parameter block nor params->buffer may be in page 1 or page 2. */
extern uint8_t __FASTCALL__ unapi_fuji_write(FujiNetParams *params);
extern uint8_t __FASTCALL__ unapi_fuji_read(FujiNetParams *params);

#endif /* UNAPI_CALL_H */
