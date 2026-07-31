;--- The disk driver's door into page 2
;
;    The driver lives in page 1 and the FujiNet transport lives in page 2, and
;    when the kernel calls this ROM only page 1 is guaranteed to be ours --
;    page 2 holds whatever RAM the caller was using. So the driver reaches the
;    transport the way any other UNAPI client would: an inter-slot call to the
;    UNAPI entry point, which pages this cartridge into page 2 for the length
;    of the call and puts the caller's RAM back afterwards.
;
;    That is also why the UNAPI specification forbids the parameter block and
;    the data buffer from living in page 1 or page 2. Both belong to the
;    implementation while the call runs. disk.c keeps them in the driver work
;    area, which the kernel allocates in page 3.
;
;    CALSLT leaves HL alone on the way in and hands back what the routine
;    returned, so the __FASTCALL__ argument in HL and the result in L both
;    pass straight through.

	PUBLIC	_unapi_fuji_write
	PUBLIC	_unapi_fuji_read

	EXTERN	UNAPI_ENTRY		; page 2
	EXTERN	GETSLT			; Disk BIOS: slot of page 1, which is us

;--- Inter-slot call. IYh = slot, IX = address, AF/BC/DE/HL passed through.

CALSLT:		equ	001Ch

;--- Function numbers from the FujiNet Firmware UNAPI specification 1.0

UNAPI_FN_WRITE:	equ	2
UNAPI_FN_READ:	equ	3

;--- Send a command, with an optional payload, to the FujiNet
;    Input:  HL = FujiNetParams *
;    Output: L  = 1 on success, 0 on failure

_unapi_fuji_write:
	ld	a,UNAPI_FN_WRITE
	jr	unapi_call

;--- Send a command and read a payload back from the FujiNet
;    Input:  HL = FujiNetParams *
;    Output: L  = 1 on success, 0 on failure

_unapi_fuji_read:
	ld	a,UNAPI_FN_READ

;--- Input: A = routine number, HL = FujiNetParams *

unapi_call:
	ld	e,a		; the routine number, out of GETSLT's way
	push	hl		; and the parameter block

	call	GETSLT		; A = this cartridge's slot
	ld	h,a
	ld	l,0
	push	hl
	pop	iy		; IYh = slot
	ld	ix,UNAPI_ENTRY

	pop	hl		; HL = FujiNetParams again

	;--- CALSLT returns with interrupts disabled, so note whether they were
	;    on. LD A,I copies IFF2 into P/V, and pushing AF puts that flag
	;    where the call cannot reach it; CALSLT leaves SP as it found it.

	ld	a,i
	push	af
	ld	a,e		; A = routine number
	call	CALSLT

	pop	de		; E = the flags saved above
	bit	2,e		; P/V: were interrupts enabled?
	ret	z
	ei
	ret
