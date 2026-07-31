;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)

	INCLUDE	"page2.inc"
	PUBLIC	DO_EXTBIO
	INCLUDE	"unapi.inc"

;*******************************
;***  EXTBIO HOOK EXECUTION  ***
;*******************************
;
;    Reached from the patched EXTBIO hook, which is an inter-slot call, so
;    this cartridge is in page 2 by the time we arrive. unapi_init.s in page 1
;    put the five bytes of hook that were here first into the driver work
;    area, and left their address in the page 2 word of our SLTWRK entry --
;    that is what OLD_HOOK below fetches.

DO_EXTBIO:
	push	hl
	push	bc
	push	af
	ld	a,d
	cp	22h
	jr	nz,JUMP_OLD
	cp	e
	jr	nz,JUMP_OLD

	;Check API ID

	ld	hl,UNAPI_ID
	ld	de,ARG
LOOP:	ld	a,(de)
	call	TOUPPER
	cp	(hl)
	jr	nz,JUMP_OLD2
	inc	hl
	inc	de
	or	a
	jr	nz,LOOP

	;A=255: Jump to old hook

	pop	af
	push	af
	inc	a
	jr	z,JUMP_OLD2

	;A=0: B=B+1 and jump to old hook

	call	OLD_HOOK
	pop	af
	pop	bc
	or	a
	jr	nz,DO_EXTBIO2
	inc	b
	ex	(sp),hl
	ld	de,2222h
	ret
DO_EXTBIO2:

	;A=1: Return A=Slot, B=Segment, HL=UNAPI entry address

	dec	a
	jr	nz,DO_EXTBIO3
	pop	hl
	call	GETSLT_P2
	ld	b,0FFh
	ld	hl,UNAPI_ENTRY
	ld	de,2222h
	ret

	;A>1: A=A-1, and jump to old hook

DO_EXTBIO3:  ;A=A-1 already done
	ex	(sp),hl
	ld	de,2222h
	ret

	;--- Jump here to execute old EXTBIO code

JUMP_OLD2:
	ld	de,2222h
JUMP_OLD:  ;Assumes "push hl,bc,af" done
	push	de
	call	OLD_HOOK
	pop	de
	pop	af
	pop	bc
	ex	(sp),hl
	ret

	;--- HL = the five bytes of EXTBIO hook this ROM displaced
	;    Modifies: AF, BC, HL and the alternate register set

OLD_HOOK:
	call	GETSLT_P2
	call	GETWRK
	ld	bc,UNAPI_WRK
	add	hl,bc
	ld	a,(hl)
	inc	hl
	ld	h,(hl)
	ld	l,a
	ret
