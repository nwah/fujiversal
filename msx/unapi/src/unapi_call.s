;--- FujiNet MSX UNAPI implementation
;
;    The disk driver talks to the FujiNet through the same UNAPI entry point
;    an external client would use, rather than calling the implementation
;    functions directly. These two shims are all that separates the driver
;    from any other UNAPI caller: load the function number and dispatch.
;
;    UNAPI_ENTRY leaves HL untouched and returns to our caller, so the
;    __FASTCALL__ argument in HL and the result in L both pass straight
;    through.

	PUBLIC	_unapi_fuji_write
	PUBLIC	_unapi_fuji_read

	EXTERN	UNAPI_ENTRY

;--- Function numbers from the FujiNet Firmware UNAPI specification 1.0

UNAPI_FN_WRITE:	equ	2
UNAPI_FN_READ:	equ	3

;--- Send a command, with an optional payload to the FujiNet
;    Input:  HL = FujiNetParams *
;    Output: L  = non-zero on success

_unapi_fuji_write:
	ld	a,UNAPI_FN_WRITE
	jp	UNAPI_ENTRY

;--- Send a command and read a payload back from the FujiNet
;    Input:  HL = FujiNetParams *
;    Output: L  = non-zero on success

_unapi_fuji_read:
	ld	a,UNAPI_FN_READ
	jp	UNAPI_ENTRY
