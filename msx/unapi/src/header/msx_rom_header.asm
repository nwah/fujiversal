	EXTERN INIT
	EXTERN DSKIO, DSKCHG, GETDPB, CHOICE, DSKFMT, DRVOFF

_header_start:
	defb "AB"	; MSX ROM Signature
	defw INIT	; Init routine

	;;  Pad to 16 bytes from the start of the header
	defs 16 - ($ - _header_start), 0

	;;  4010h: the disk driver jump table the MSX-DOS kernel calls into.
	;;  The routines themselves are in disk_entry.s.
	jp DSKIO
	jp DSKCHG
	jp GETDPB
	jp CHOICE
	jp DSKFMT
	jp DRVOFF
