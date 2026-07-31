;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)

	INCLUDE	"page2.inc"
	PUBLIC	UNAPI_ID, APIINFO, FN_TABLE, MAX_FN
	INCLUDE	"unapi.inc"
	INCLUDE	"fuji_call.inc"

	;--- Specification identifier (up to 15 chars)

UNAPI_ID:
	db	"FUJINET",0

	;--- Implementation identifier (up to 63 chars and zero terminated)

APIINFO:
	db	"ROM implementation of FUJINET UNAPI",0

;--- Standard routines addresses table
;    Numbering follows the FujiNet Firmware UNAPI specification 1.0: 0 is the
;    mandatory info routine, 2 is write and 3 is read. The specification
;    assigns nothing to 1, so that slot returns without touching a register.

FN_TABLE:
	dw	FN_INFO          ; 0  FN_GETINFO
	dw	FN_UNDEFINED     ; 1  unassigned by the specification
	dw	FN_WRITE         ; 2  FN_CALL_WRITE
	dw	FN_READ          ; 3  FN_CALL_READ

;--- The highest routine number in the table. Written out rather than
;    measured from $: this section is orged into a bank, and the address
;    arithmetic that would give does not survive that.

MAX_FN:	equ	3


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

;--- Routines 2 and 3: hand HL straight to the transport, and give it a
;    running clock to wait on.
;
;    Everything reaches this entry point through an inter-slot call, and the
;    MSX inter-slot call routines disable interrupts. The transport times its
;    waits off JIFFY, which the frame interrupt is the only thing that
;    advances, so with interrupts left off a device that never answers is
;    waited on for ever. Turn them on for the length of the call and hand the
;    machine back the way it arrived.

FN_WRITE:
	ei
	call	_fujiF5_write
	di
	ret

FN_READ:
	ei
	call	_fujiF5_read
	di
	ret
