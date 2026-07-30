	INCLUDE	"portio.inc"
	PUBLIC	_port_putbuf_slip

;-----------------------------------------------------------------------------
; uint16_t port_putbuf_slip(const void *buf, uint16_t len, uint16_t slots)
; Encode and transmit a SLIP-framed packet  (__CALLEE__ calling convention)
;
; Operation:
;   - 0xC0 -> sends 0xDB 0xDC
;   - 0xDB -> sends 0xDB 0xDD
;   - Other -> sends as-is
;
; slots carries the two values to write to the primary slot register: the
; low byte selects the caller's RAM in page 2, the high byte selects this
; ROM's slot so the IO window is reachable. Page 2 is the ROM's on entry and
; on exit; each source byte is fetched with RAM paged back in, which is what
; allows a source buffer in page 2.
;
; Parameters (pushed left-to-right, callee cleans stack):
;
; Stack on entry:
;   (SP+0) = return address
;   (SP+2) = slots
;   (SP+4) = len
;   (SP+6) = buf
;
; Returns:
;   HL = number of encoded bytes transmitted
;
; Register usage:
;   A  = current byte being processed
;   BC = remaining bytes to encode (counts down)
;   DE = encoded byte count (moved to HL on return)
;   HL = source buffer pointer
;   IX = parameter base
;-----------------------------------------------------------------------------

	PUTB_PARAM_SLOTS	equ	4
	PUTB_PARAM_LEN		equ	6
	PUTB_PARAM_BUF		equ	8

	ARG_BYTE_LEN		equ	6	; 3 words

_port_putbuf_slip:
	PUSH	IX			; Callee-save IX
	LD	IX, 0
	ADD	IX, SP			; IX is our stable base for parameters

	; Stack at this point:
	;   (IX+0) = saved IX
	;   (IX+2) = return address
	;   (IX+4) = slots
	;   (IX+6) = len
	;   (IX+8) = buf

	LD	BC, (IX + PUTB_PARAM_LEN)
	LD	HL, (IX + PUTB_PARAM_BUF)
	LD	DE, 0			; DE counts encoded bytes

	; Check for zero length
	LD	A, B
	OR	C
	JR	Z, slip_put_end

slip_put_loop:
	; Page 2 currently holds the IO window, so give it back to RAM long
	; enough to fetch the source byte, then take it again to transmit.
	LD	A, (IX + PUTB_PARAM_SLOTS)	; page 2 = caller's RAM
	OUT	(SLOT_PORT), A
	LD	A, (HL)			; Load byte from buffer
	INC	HL			; Advance pointer
	PUSH	AF
	LD	A, (IX + PUTB_PARAM_SLOTS + 1)	; page 2 = IO window
	OUT	(SLOT_PORT), A
	POP	AF

	CP	SLIP_END		; 0xC0?
	JR	Z, slip_put_encode_end
	CP	SLIP_ESC		; 0xDB?
	JR	Z, slip_put_encode_esc

slip_put_send:
	LD	(IO_PUTC), A		; Write to memory-mapped output
	INC	DE			; Count encoded byte
	DEC	BC
	LD	A, B
	OR	C
	JR	NZ, slip_put_loop

slip_put_end:
	LD	SP, IX			; Discard anything left on the stack
	POP	IX			; Restore IX

	POP	BC			; BC = return address

	; Clean the arguments off the stack
	LD	HL, ARG_BYTE_LEN
	ADD	HL, SP
	LD	SP, HL			; SP is now cleaned

	PUSH	BC			; Put return address back
	EX	DE, HL			; HL = encoded byte count
	RET

slip_put_encode_end:
	; Encode 0xC0 as 0xDB 0xDC
	LD	A, SLIP_ESC
	LD	(IO_PUTC), A
	INC	DE
	LD	A, SLIP_ESC_END
	JR	slip_put_send

slip_put_encode_esc:
	; Encode 0xDB as 0xDB 0xDD
	LD	A, SLIP_ESC
	LD	(IO_PUTC), A
	INC	DE
	LD	A, SLIP_ESC_ESC
	JR	slip_put_send
