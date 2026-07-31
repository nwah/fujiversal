; FujiNet disk driver for the MSX Disk BIOS
;
;    The kernel in bios/diskbios.asm does the filesystem, Disk BASIC and DOS
;    work and calls down to the routines here for anything that touches the
;    hardware. Each drive is one FujiNet device slot: drive 0 is D1, drive 1
;    is D2 and so on.
;
;    The routines that move data are written in C (disk.c) and reached through
;    the register block described below. The kernel passes everything in
;    registers, so each entry point pushes them to form an MsxRegs block, hands
;    its address to the C routine, then pops the block straight back into the
;    CPU. A routine returns a value simply by writing to the block.

	PUBLIC	INIHRD, DRIVES, INIENV, MTOFF, OEMSTA
	PUBLIC	DSKIO, DSKCHG, GETDPB, CHOICE, DSKFMT
	PUBLIC	MYSIZE, SECLEN, DEFDPB
	PUBLIC	disk_get_work, _disk_get_work

	EXTERN	_disk_io, _disk_chg, _disk_getdpb, _disk_choice, _disk_fmt

;--- Announcing the UNAPI implementation in page 2 is part of coming up, see
;    unapi_init.s.

	EXTERN	UNAPI_INSTALL

;--- Kernel routines this driver uses. GETWRK hands back the work area the
;    kernel allocated for us, see MYSIZE. GETSLT is exported for the page 1
;    UNAPI code, which needs to know which slot this cartridge is in; it is
;    the kernel's own routine and reads the slot of page 1, which is us.

	EXTERN	GETWRK
	PUBLIC	GETSLT

;--- One drive per FujiNet disk device slot. The kernel refuses more than 8,
;    and rather fewer than that in practice -- see PLAN.md. Keep in step with
;    FN_DRIVES in disk.h.

FN_MAX_DEV:	equ	2

;--- Work area the kernel allocates on our behalf and remembers in SLTWRK.
;    It holds the displaced EXTBIO hook, one 32 bit mount time per drive (how
;    DSKCHG spots a disk being swapped underneath us), the parameter block
;    every UNAPI call goes out with, and a sector sized bounce buffer. The
;    last two are there because the kernel's own memory is in page 3 and a
;    UNAPI call may not be handed anything in page 1 or page 2. Keep in sync
;    with DiskWork in disk.h, which has a compile time check on this number.

MYSIZE:		equ	536

;--- Largest sector this driver will ever ask the kernel to buffer

SECLEN:		equ	512


;--- INIHRD: initialise the hardware
;    There is none to initialise. The FujiNet is reached through a memory
;    mapped window that is always there.

INIHRD:
	ret


;--- DRIVES: how many drives are attached
;    In:  Zx set to report physical drives, reset to report at least two
;    Out: L = number of drives
;
;    Always the full set of device slots. They cost nothing when empty, and a
;    fixed count keeps drive letters stable across mounts.

DRIVES:
	ld	l,FN_MAX_DEV
	ret


;--- INIENV: initialise the driver work area
;    The kernel has just allocated MYSIZE bytes for us. Clear them so every
;    drive starts out with no mount time on record, which makes the first
;    DSKCHG on each drive report a change and build a fresh DPB.
;
;    This is also the driver's one chance to run code at boot with the work
;    area in hand, so the UNAPI half of the ROM announces itself here.

INIENV:
	call	GETWRK		; HL = our work area
	push	hl
	ld	d,h
	ld	e,l
	inc	de
	ld	(hl),0
	ld	bc,MYSIZE-1
	ldir
	pop	hl
	jp	UNAPI_INSTALL


;--- MTOFF: stop the drive motors
;    There are none to stop.

MTOFF:
	ret


;--- OEMSTATEMENT: driver specific CALL statements
;    In:  HL = basic pointer
;    Out: Cy set if the statement was not recognised
;
;    None are offered, so every statement is left for someone else.

OEMSTA:
	scf
	ret


;--- DSKIO (4010h): transfer sectors
;    In:  Cy = 0 read / 1 write, A = drive, B = sectors, C = media,
;         DE = first sector, HL = transfer address
;    Out: Cy = 0 ok, else Cy = 1, A = error code, B = sectors left

DSKIO:
	ld	ix,_disk_io
	jr	DISK_ENTER

;--- DSKCHG (4013h): has the disk been swapped?
;    In:  A = drive, B = 0, C = media, HL = DPB address
;    Out: Cy = 0 and B = 1 unchanged / 0 unknown / -1 changed
;
;    The DOS1 kernel expects the DPB rebuilt when the answer is "changed" or
;    "unknown", which disk_chg does.

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
;    In:  A = choice (from 1), D = drive, HL = work area, BC = its length
;    Out: Cy = 0 ok, else Cy = 1 and A = error code
;
;    Unlike the 4010h version of this call the kernel hands us a work area
;    here, which is where the sectors get built. That is why this driver
;    carries no sector templates of its own.

DSKFMT:
	ld	ix,_disk_fmt
	jr	DISK_ENTER


;--- Shared prologue and epilogue
;    In: IX = C routine, registers as the kernel passed them
;
;    The pushes below build the MsxRegs block in the order a Z80 pushes, so
;    that popping it again restores every register the routine did not mean
;    to change.

DISK_ENTER:
	push	hl
	push	de
	push	bc
	push	af		; A and the carry flag land at the lowest address

	ld	hl,0
	add	hl,sp		; HL = the MsxRegs block we just built

	call	DISK_CALL_IX

	pop	af
	pop	bc
	pop	de
	pop	hl
	ret

DISK_CALL_IX:
	jp	(ix)


;--- Fetch the work area the kernel allocated for this driver
;    Out: HL = work area
;    Modifies: AF, BC, DE, HL, IX

disk_get_work:
_disk_get_work:
	jp	GETWRK


;--- Default Drive Parameter Block
;
;    The kernel copies 21 bytes from here into every drive's DPB slot at boot,
;    before it has asked any drive what it actually holds, so this only has to
;    be a shape the kernel can work with until the first GETDPB. It describes
;    the usual MSX 720K disk. DEFDPB points one byte low because the copy
;    starts at the drive number rather than the media descriptor.

DPB_DEFAULT:
	defb	0F9h		; media descriptor: 720K, double sided
	defw	512		; bytes per sector
	defb	0Fh		; directory entries per sector - 1
	defb	4		; bits set in the mask above
	defb	1		; sectors per cluster - 1
	defb	2		; bits set in the mask above, plus one
	defw	1		; first FAT sector
	defb	2		; number of FATs
	defb	112		; root directory entries
	defw	14		; first data sector
	defw	714		; highest cluster number
	defb	3		; sectors per FAT
	defw	7		; first root directory sector

DEFDPB:		equ	DPB_DEFAULT-1
