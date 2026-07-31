	extern	_port_getc, _port_getc_timeout, _port_discard_until
	extern	_port_getbuf, _port_get_until
	extern	_port_putc, _port_putbuf

	extern	getbuf_done

	extern	timeout_init, timeout_check, timeout_cleanup

	; Primary slot select register. The IO window below lives in page 2, so
	; reaching it means paging this ROM's slot over whatever RAM is there.
	; Routines that touch a caller supplied buffer hand page 2 back for the
	; duration of each access, which is what lets a buffer live in page 2.
	SLOT_PORT	EQU	0xA8

	IO_OFFSET	EQU	(0x8000 + 0x3FFC)
	IO_GETC		EQU	(IO_OFFSET + 0)
	IO_STATUS	EQU	(IO_OFFSET + 1)
	IO_PUTC		EQU	(IO_OFFSET + 2)
	IO_CONTROL	EQU	(IO_OFFSET + 3)

	SLIP_END	EQU	0xC0
	SLIP_ESC	EQU	0xDB
	SLIP_ESC_END	EQU	0xDC
	SLIP_ESC_ESC	EQU	0xDD
