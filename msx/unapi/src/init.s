;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)

	INCLUDE	"const.inc"
	INCLUDE	"disk.inc"
	PUBLIC	INIT

INIT:
	;--- Initialize EXTBIO hook if necessary

	ld	a,(HOKVLD)
	bit	0,a
	jr	nz,OK_INIEXTB

	ld	hl,EXTBIO
	ld	de,EXTBIO+1
	ld	bc,5-1
	ld	(hl),0C9h  ;code for RET
	ldir

	or	1
	ld	(HOKVLD),a
OK_INIEXTB:

	;--- Save previous EXTBIO hook

	if	ALLOC_P3

	ld	hl,5
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
	ld	bc,5
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

	;--- Reserve the disk driver work area just below HIMEM.
	;    It has to be in page 3: a FujiNet transfer pages this ROM over
	;    page 2, and the sector buffer has to stay reachable while it is.
	;    BASIC works out its own memory from HIMEM once the slot scan is
	;    over, so lowering it here keeps the area to ourselves.

	ld	hl,(HIMEM)
	ld	de,-(DISK_WORK_SIZE + 1)
	add	hl,de
	ld	(HIMEM),hl
	inc	hl		; first byte we own

	;--- Record it in our SLTWRK entry, just past the saved EXTBIO hook,
	;    which is how disk_get_work() finds it later.

	push	hl
	call	GETSLT
	call	GETWRK
	ld	de,DISK_WORK_SLOT
	add	hl,de
	pop	de
	ld	(hl),e
	inc	hl
	ld	(hl),d

	;--- Mark the area as not yet cleared. We must not touch a byte of it
	;    here: during the slot scan the boot stack is still sitting just
	;    below HIMEM, which is precisely the memory we have taken. BASIC
	;    moves its stack below the new HIMEM once the scan is over, so
	;    disk_get_work() does the clearing on the first call instead.

	inc	hl
	ld	(hl),0

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
