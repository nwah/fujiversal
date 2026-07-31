	extern	_port_getc, _port_getc_timeout
	extern	_port_putc

	extern	timeout_init, timeout_check, timeout_cleanup

	; The IO window is the last four bytes of page 2. Everything that
	; touches it lives in page 2 as well, and page 2 code only ever runs
	; with this ROM paged in -- that is what the inter-slot call to the
	; UNAPI entry point arranges -- so the window is simply there and no
	; routine here touches the slot register. The UNAPI specification
	; forbids a caller's buffer in page 1 or page 2 for the same reason:
	; both pages belong to the implementation for the length of a call.

	IO_OFFSET	EQU	(0x8000 + 0x3FFC)
	IO_GETC		EQU	(IO_OFFSET + 0)
	IO_STATUS	EQU	(IO_OFFSET + 1)
	IO_PUTC		EQU	(IO_OFFSET + 2)
	IO_CONTROL	EQU	(IO_OFFSET + 3)

	SLIP_END	EQU	0xC0
	SLIP_ESC	EQU	0xDB
	SLIP_ESC_END	EQU	0xDC
	SLIP_ESC_ESC	EQU	0xDD
