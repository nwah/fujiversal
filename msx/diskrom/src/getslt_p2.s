;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)

	INCLUDE	"page2.inc"
	PUBLIC	GETSLT_P2
	INCLUDE	"unapi.inc"

;--- Get the slot connected on page 2, which is this ROM
;    Input:  -
;    Output: A = Slot number
;    Modifies: AF and the alternate register set
;
;    Konamiman's original read the page 1 bits, because a UNAPI ROM is
;    normally a 16K cartridge sitting there. This implementation is the top
;    half of a 32K cartridge and runs with only page 2 guaranteed to be ours:
;    an inter-slot call to the entry point pages us in there and nowhere else.
;    So the slot has to be read from the page 2 bits of the primary slot
;    register, and from the page 2 bits of SLTTBL if the slot is expanded.
;    The slot number it yields is the same one page 1 would report, since the
;    whole cartridge is one slot -- it is only the place to look that differs.

GETSLT_P2:
	di
	exx
	in	a,(0A8h)
	and	00110000b
	rrca
	rrca
	rrca
	rrca
	ld	c,a  ;C = Slot
	ld	b,0
	ld	hl,EXPTBL
	add	hl,bc
	bit	7,(hl)
	jr	z,NOEXP2
EXP2:	inc	hl
	inc	hl
	inc	hl
	inc	hl
	ld	a,(hl)
	and	00110000b
	rrca
	rrca
	or	c
	or	80h
	ld	c,a
NOEXP2:	ld	a,c
	exx
	ei
	ret
