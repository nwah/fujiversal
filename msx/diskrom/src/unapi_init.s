;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)
;
;    Announce the UNAPI implementation in page 2 by patching the EXTBIO hook,
;    which is how MSX-UNAPI clients discover an API. This half of it has to be
;    in page 1: the Disk BIOS calls INIENV, and page 2 is not ours during a
;    call from the kernel.
;
;    Konamiman's original ran from a ROM's own INIT and kept the displaced
;    hook in SLTWRK. It cannot go there here -- the Disk BIOS keeps the driver
;    work area pointer in the page 1 word of the same eight byte entry -- so
;    the five bytes live at the head of that work area instead, and the page 2
;    word points at them. do_extbio.s reads it back the same way.

	PUBLIC	UNAPI_INSTALL

	EXTERN	GETSLT			; Disk BIOS: slot of page 1, which is us
	EXTERN	DO_EXTBIO		; page 2

HOKVLD:		equ	0FB20h
EXTBIO:		equ	0FFCAh
SLTWRK:		equ	0FD09h
UNAPI_WRK:	equ	4		; page 2 word of an SLTWRK entry
HOOK_LEN:	equ	5

;--- Install the hook
;    In: HL = driver work area; its first HOOK_LEN bytes are ours
;    Modifies: everything

UNAPI_INSTALL:
	push	hl

	;--- An unhooked EXTBIO is five bytes of nothing in particular. Make it
	;    a RET before anyone can be sent there.

	ld	a,(HOKVLD)
	bit	0,a
	jr	nz,HOOK_VALID
	ld	hl,EXTBIO
	ld	de,EXTBIO+1
	ld	bc,HOOK_LEN-1
	ld	(hl),0C9h		; RET
	ldir
	or	1
	ld	(HOKVLD),a
HOOK_VALID:

	;--- Keep whatever was there, so DO_EXTBIO can chain to it

	pop	de			; DE = work area
	push	de
	ld	hl,EXTBIO
	ld	bc,HOOK_LEN
	ldir

	;--- and leave its address where page 2 can find it

	call	GETSLT			; A = this cartridge's slot
	call	SLTWRK_ENTRY		; HL = our eight bytes of SLTWRK
	ld	bc,UNAPI_WRK
	add	hl,bc
	pop	de			; DE = work area
	ld	(hl),e
	inc	hl
	ld	(hl),d

	;--- Point EXTBIO at DO_EXTBIO in page 2. RST 30h is the inter-slot
	;    call that gets us there with the cartridge paged in.
	;
	;    GETSLT is the kernel's, and it works out of HL, so ask it for the
	;    slot before there is anything in HL worth keeping.

	call	GETSLT
	ld	e,a			; E = this cartridge's slot

	di
	ld	hl,EXTBIO
	ld	(hl),0F7h		; RST 30h
	inc	hl
	ld	(hl),e			; our slot
	inc	hl
	ld	de,DO_EXTBIO
	ld	(hl),e
	inc	hl
	ld	(hl),d
	inc	hl
	ld	(hl),0C9h		; RET
	ei
	ret

;--- Obtain the slot work area (8 bytes) in SLTWRK
;    In:  A  = slot number
;    Out: HL = work area address
;    Modifies: AF, BC

SLTWRK_ENTRY:
	ld	b,a
	rrca
	rrca
	rrca
	and	01100000b
	ld	c,a			; C = slot * 32
	ld	a,b
	rlca
	and	00011000b		; A = subslot * 8
	or	c
	ld	c,a
	ld	b,0
	ld	hl,SLTWRK
	add	hl,bc
	ret
