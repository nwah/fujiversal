;--- FujiNet MSX UNAPI implementation
;    Based on unapi-rom.asm by Konamiman, 5-2019 (MIT License)
;
;    Shared declarations for the UNAPI half of the ROM, all of which lives in
;    page 2. The disk BIOS in page 1 has its own GETSLT; this one is called
;    GETSLT_P2 both to keep the linker happy and because it has to read a
;    different pair of bits.

	EXTERN	UNAPI_ID
	EXTERN	TOUPPER
	EXTERN	GETSLT_P2
	EXTERN	GETWRK
	EXTERN	DO_EXTBIO
	EXTERN	UNAPI_ENTRY
	EXTERN	FN_TABLE
	EXTERN	MAX_FN

;--- System variables

HOKVLD:	equ	0FB20h
EXPTBL:	equ	0FCC1h
EXTBIO:	equ	0FFCAh
SLTWRK:	equ	0FD09h
ARG:	equ	0F847h

;--- API version and implementation version

API_V_P:	equ	1
API_V_S:	equ	0
ROM_V_P:	equ	1
ROM_V_S:	equ	0

;--- Must be either zero (if no implementation-specific functions available),
;    or 128 to 254

MAX_IMPFN:	equ	0

;--- SLTWRK gives every slot eight bytes, one word per page. The Disk BIOS
;    keeps the driver work area pointer in the page 1 word at offset 2, so
;    UNAPI takes the page 2 word -- which is the one that describes the page
;    it actually occupies. It holds the address of the five bytes of EXTBIO
;    hook that were there before this ROM patched it.

UNAPI_WRK:	equ	4
