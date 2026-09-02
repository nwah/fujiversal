;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)

	INCLUDE	"const.inc"
	PUBLIC	INIT

INIT:
	;--- Initialize the hooks if necessary
	;
	;    Bit 0 of HOKVLD says the hook block at EXTBIO has been initialized,
	;    and that block is three hooks long: EXTBIO, then DISINT and ENAINT.
	;    Setting the bit tells everyone that comes later -- on an MSX2 that
	;    includes the disk ROM, which initialises the same fifteen bytes and
	;    skips them if the bit is already set -- that all three are safe to
	;    call. Filling in only the five bytes of EXTBIO leaves the other two
	;    as whatever RAM came up as, and the machine dies the first time
	;    something disables interrupts through them: on both a Philips NMS
	;    8250 and openMSX's Boosted_MSX2_EN it never reaches BASIC.

	ld	a,(HOKVLD)
	bit	0,a
	jr	nz,OK_INIEXTB

	ld	hl,EXTBIO
	ld	de,EXTBIO+1
	ld	bc,HOOKS_LEN-1
	ld	(hl),0C9h  ;code for RET
	ldir

	or	1
	ld	(HOKVLD),a
OK_INIEXTB:

	;--- Save previous EXTBIO hook

	if	ALLOC_P3

	ld	hl,HOOK_LEN
	call	ALLOC
	push	hl
	call	GETSLT
	call	GETWRK
	pop	de
	ld	(hl),e
	inc	hl
	ld	(hl),d

	else

	call	GETSLT
	call	GETWRK
	ex	de,hl

	endif

	ld	hl,EXTBIO
	ld	bc,HOOK_LEN
	ldir

	;--- Patch EXTBIO hook

	di
	ld	a,0F7h  ;code for "RST 30h"
	ld	(EXTBIO),a
	call	GETSLT
	ld	(EXTBIO+1),a
	ld	hl,DO_EXTBIO
	ld	(EXTBIO+2),hl
	ld	a,0C9h
	ld	(EXTBIO+4),a
	ei

	;>>> UNAPI initialization finished, now perform
	;    other ROM initialization tasks.

ROM_INIT:

	;TODO: extend (or replace) with other initialization code as needed by your implementation

	;--- Show informative message

	ld	hl,INITMSG
PRINT_LOOP:
	ld	a,(hl)
	or	a
	jp	z,INIT2
	call	CHPUT
	inc	hl
	jr	PRINT_LOOP
INIT2:

	ret
