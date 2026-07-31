;--- The MSX Disk BIOS is what sits at 4000h, so it stands in as this ROM's
;    header: it carries the "AB" signature, the INIT vector and the jump
;    table at 4010h, and pulls in src/driver.asm at its tail. Everything the
;    linker places after it is driver code.

	INCLUDE	"../bios/diskbios.asm"
