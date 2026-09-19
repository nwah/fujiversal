;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)

	PUBLIC	INITMSG, UNAPI_ID, APIINFO, FN_TABLE, MAX_FN
	INCLUDE	"const.inc"
	INCLUDE "fuji_call.inc"

	;--- Specification identifier (up to 15 chars)

UNAPI_ID:
	db	"FUJINET",0

	;--- Implementation identifier (up to 63 chars and zero terminated)

APIINFO:
	db	"ROM implementation of FUJINET UNAPI",0

	;--- Other data

INITMSG:
	db	13,10,"FujiNet UNAPI ROM 1.0B",13,10
	db	13,10
	db	0

;--- Standard routines addresses table
;    Numbering follows the FujiNet Firmware UNAPI specification 1.0: 0 is the
;    mandatory info routine, 2 is write and 3 is read. The specification
;    assigns nothing to 1, so that slot returns without touching a register.

FN_TABLE:
	dw	FN_INFO          ; 0  FN_GETINFO
	dw	FN_UNDEFINED     ; 1  unassigned by the specification
	dw	_fujiF5_write    ; 2  FN_CALL_WRITE
	dw	_fujiF5_read     ; 3  FN_CALL_READ
MAX_FN:	equ	($ - FN_TABLE) / 2 - 1


;--- Mandatory routine 0: return API information
;    Input:  A  = 0
;    Output: HL = Descriptive string for this implementation, on this slot, zero terminated
;            DE = API version supported, D.E
;            BC = This implementation version, B.C.
;            A  = 0 and Cy = 0

FN_INFO:
	ld	bc,256*ROM_V_P+ROM_V_S
	ld	de,256*API_V_P+API_V_S
	ld	hl,APIINFO
	xor	a
	ret

;--- A routine number the specification leaves unassigned. The specification
;    requires AF, BC, DE and HL to come back unmodified, and the dispatcher
;    has already restored them by the time it jumps here.

FN_UNDEFINED:
	ret

;--- Routines 2 and 3 are the transport itself, entered with interrupts
;    exactly as the client left them -- not force-enabled as they used to
;    be, since an interrupt taken while this ROM sits in the client's page 1
;    doesn't survive a client whose own code the machine needs at interrupt
;    time (a disk kernel, say). Timeouts count loops instead of JIFFY now,
;    so nothing here needs interrupts on. See timeout.s.
