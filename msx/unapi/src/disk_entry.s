;--- FujiNet MSX disk driver entry points
;
;    The MSX-DOS kernel calls these through the jump table at 4010h with
;    everything in registers. Each stub pushes the incoming registers to
;    form a MsxRegs block, hands its address to the matching C routine,
;    then pops the block straight back into the CPU. A routine therefore
;    returns a value simply by writing to the block.
;
;    Everything below the jump table entries runs on a private stack, since
;    the kernel only promises a small one and an interrupt can land on it
;    mid transfer.

	PUBLIC	DSKIO, DSKCHG, GETDPB, CHOICE, DSKFMT, DRVOFF
	PUBLIC	DRIVES
	PUBLIC	disk_get_work, _disk_get_work

	EXTERN	_disk_io, _disk_chg, _disk_getdpb, _disk_choice, _disk_fmt
	EXTERN	GETSLT, GETWRK

	INCLUDE	"disk.inc"

;--- DSKIO (4010h): transfer sectors
;    In:  Cy = 0 read / 1 write, A = drive, B = sectors, C = media,
;         DE = first sector, HL = transfer address
;    Out: Cy = 0 ok, else Cy = 1, A = error code, B = sectors left

DSKIO:
	ld	ix,_disk_io
	jr	DISK_ENTER

;--- DSKCHG (4013h): has the disk been swapped?
;    In:  A = drive, C = media, HL = DPB address
;    Out: Cy = 0 and B = 1 unchanged / 0 unknown / -1 changed

DSKCHG:
	ld	ix,_disk_chg
	jr	DISK_ENTER

;--- GETDPB (4016h): fill in the drive parameter block
;    In:  A = drive, B = first FAT byte, C = media, HL = DPB address
;    Out: DPB filled from HL+1

GETDPB:
	ld	ix,_disk_getdpb
	jr	DISK_ENTER

;--- CHOICE (4019h): format options offered by DSKFMT
;    Out: HL = zero terminated string, or 0 for none

CHOICE:
	ld	ix,_disk_choice
	jr	DISK_ENTER

;--- DSKFMT (401Ch): lay a fresh filesystem on the image
;    In:  A = choice (from 1), D = drive
;    Out: Cy = 0 ok, else Cy = 1 and A = error code

DSKFMT:
	ld	ix,_disk_fmt
	jr	DISK_ENTER

;--- DRVOFF (401Fh): stop the drive motors. There are none to stop.

DRVOFF:
	ret

;--- Number of drives this interface provides, one per FujiNet device slot.
;    Not part of the 4010h table; a DOS kernel linked against this driver
;    calls it directly.
;    Out: A = L = drive count

DRIVES:
	ld	a,FN_MAX_DEV
	ld	l,a
	ret

;--- Shared prologue and epilogue
;    In: IX = C routine, registers as the kernel passed them

DISK_ENTER:
	push	hl
	push	de
	push	bc
	push	af		; A and the carry flag land at the lowest address

	ld	hl,0
	add	hl,sp		; HL = the MsxRegs block we just built
	push	hl
	pop	iy

	call	DISK_DISPATCH
	jr	z,DISK_NO_WORK

	pop	af
	pop	bc
	pop	de
	pop	hl
	ret

;--- No work area means INIT never ran, so there is nothing to talk to.
;    Restore what the caller passed and report a general failure.

DISK_NO_WORK:
	pop	af
	pop	bc
	pop	de
	pop	hl
	ld	a,DISK_ERR_OTHER
	scf
	ret

;--- Run a C routine on the driver's private stack
;    In:  IY = MsxRegs block, IX = routine (__FASTCALL__, HL = block)
;    Out: Z set if the work area has not been allocated

DISK_DISPATCH:
	call	disk_get_work
	ld	a,h
	or	l
	ret	z

	ld	bc,DISK_WORK_SIZE
	add	hl,bc		; HL = top of the private stack, at the end
	ex	de,hl
	ld	hl,0
	add	hl,sp		; HL = the caller's stack pointer
	ex	de,hl		; HL = private stack, DE = caller's stack
	ld	sp,hl
	push	de		; park the caller's stack pointer on ours

	push	iy
	pop	hl		; HL = MsxRegs block
	call	DISK_CALL_IX

	pop	hl
	ld	sp,hl		; back to the caller's stack
	or	0ffh		; clear Z: the routine ran
	ret

DISK_CALL_IX:
	jp	(ix)

;--- Fetch the work area address INIT left in our SLTWRK entry, clearing the
;    area if this is the first call since INIT.
;    Out: HL = work area, or 0 if INIT has not run
;    Modifies: AF, BC, DE, HL

disk_get_work:
_disk_get_work:
	call	GETSLT
	call	GETWRK
	ld	de,DISK_WORK_SLOT
	add	hl,de
	ld	e,(hl)
	inc	hl
	ld	d,(hl)		; DE = work area
	inc	hl		; HL = the cleared marker
	ld	a,d
	or	e
	jr	z,WORK_NONE

	ld	a,(hl)
	cp	DISK_READY_MARK
	jr	z,WORK_READY

	;--- First call since INIT, which had to leave the area untouched
	;    because the boot stack was still in it. By now BASIC has moved
	;    its stack below our HIMEM, so the memory is finally ours.

	ld	(hl),DISK_READY_MARK
	push	de
	ld	h,d
	ld	l,e
	inc	de
	ld	(hl),0
	ld	bc,DISK_WORK_SIZE-1
	ldir
	pop	hl
	ret

WORK_READY:
	ex	de,hl
	ret

;--- INIT never ran, so there is no area and no address to hand back

WORK_NONE:
	ld	h,d
	ld	l,e
	ret
