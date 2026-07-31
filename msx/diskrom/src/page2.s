;--- Page 2: the FujiNet transport and the UNAPI implementation
;
;    The 32K ROM spans 4000h-BFFFh. Page 1 carries the MSX Disk BIOS and the
;    disk driver; everything here is page 2, reached only through the UNAPI
;    entry point, which callers get to with an inter-slot call. The driver in
;    page 1 goes through the same door as any other client, see unapi_call.s.
;
;    This module owns the section origin. Other page 2 modules include
;    page2.inc, which names the section but does not org it.
;
;    The IO window sits at BFFCh-BFFFh, the last four bytes of page 2, and
;    nothing may be placed there. The Makefile checks the section size after
;    every link rather than trusting anyone to remember.

	INCLUDE	"page2.inc"

	org	0x018000	; bank 1, address 8000h
