	include	"portio.inc"
	public	_port_getbuf_slip_dual

;-----------------------------------------------------------------------------
; Macro to wait for a character with timeout
; On entry: IX is the parameter base
; On exit : A = received byte
; Destroys: F, HL
;
; The timeout is reloaded here rather than by the caller: port_getc_timeout
; returns the byte in the same register it takes the timeout in, so anything
; that let HL carry over would be waiting for the length of the byte it read
; last, and a byte of zero would end the frame early.
;-----------------------------------------------------------------------------
SLIPD_WAIT_CHAR macro timeout_label
	ld	hl,(ix + SLIPD_PARAM_TIMEOUT)
	call	_port_getc_timeout
	ld	a,h		; check high byte
	or	a
	jp	nz,timeout_label	; if high byte is set then timed out
	ld	a,l
ENDM

;-----------------------------------------------------------------------------
; uint16_t port_getbuf_slip_dual(void *hdr_buf,  uint16_t hdr_len,
;                                void *data_buf, uint16_t data_len,
;                                uint16_t timeout, uint16_t slots)
;
; Read and decode a SLIP-framed packet into two buffers.
; (__callee__ calling convention -- callee cleans the stack)
;
; slots carries the two values to write to the primary slot register: the
; low byte selects the caller's RAM in page 2, the high byte selects this
; ROM's slot so the IO window is reachable. Page 2 is the ROM's on entry and
; on exit; each stored byte hands it back to RAM just long enough to write,
; which is what allows a destination buffer in page 2.
;
; Operation:
; - First hdr_len bytes go to hdr_buf
; - Remaining bytes go to data_buf
;
;   Phase 1 -- Sync:  discard bytes until SLIP_END (0xC0) seen
;   Phase 2 -- Skip:  discard any further consecutive SLIP_END bytes
;   Phase 3 -- Decode loop:
;       SLIP_END          -> end of frame, return
;       SLIP_ESC + 0xDC   -> store 0xC0
;       SLIP_ESC + 0xDD   -> store 0xDB
;       SLIP_ESC + other  -> store as-is (lenient)
;       Anything else     -> store as-is
;   First hdr_len decoded bytes -> hdr_buf
;   Remaining decoded bytes     -> data_buf (up to data_len bytes)
;
; Stack on entry (left-to-right push, rightmost param nearest SP):
;   (SP+0)  = return address
;   (SP+2)  = slots    (rightmost, pushed last)
;   (SP+4)  = timeout
;   (SP+6)  = data_len
;   (SP+8)  = data_buf
;   (SP+10) = hdr_len
;   (SP+12) = hdr_buf          (leftmost, pushed first)
;
; Convert ms to jiffies before calling:
;   PAL  (50 Hz): jiffies = ms / 20
;   NTSC (60 Hz): jiffies = ms / 17  (approx)
;
; Returns:
;   HL = total decoded bytes written (header + data)
;-----------------------------------------------------------------------------

	ARG_BYTE_LEN	equ	12	; 6 words

	; How much may be thrown away before a frame starts, in units of 256
	; bytes. Comfortably more than one packet, so a late reply still syncs.
	SLIPD_JUNK_MAX	equ	8

	SLIPD_PARAM_SLOTS	equ	6
	SLIPD_PARAM_TIMEOUT	equ	8
	SLIPD_PARAM_DATA_LEN	equ	10
	SLIPD_PARAM_DATA_BUF	equ	12
	SLIPD_PARAM_HDR_LEN	equ	14
	SLIPD_PARAM_HDR_BUF	equ	16

_port_getbuf_slip_dual:
	push	ix			; Callee-save IX
	push	iy			; Callee-save IY

	; Stack at this point:
	;   (SP+0)  = saved IY
	;   (SP+2)  = saved IX
	;   (SP+4)  = Return Address
	;   (SP+6)  = timeout
	;   (SP+8)  = data_len
	;   (SP+10) = data_buf
	;   (SP+12) = hdr_len
	;   (SP+14) = hdr_buf

	ld	ix, 0
	add	ix, sp	       		; IX is now our stable base for parameters
	ld	iy, 0			; IY is total bytes written

	ld	de,(ix + SLIPD_PARAM_HDR_BUF)	; DE is buffer pointer

	; Check for zero total length
	ld	hl,(ix + SLIPD_PARAM_HDR_LEN)
	ld	a, h
	or	l
	ld	hl,(ix + SLIPD_PARAM_DATA_LEN)
	or	h
	or	l
	jp	z, slipd_done		; Both zero -- skip timeout_init entirely

	ld	de,(ix + SLIPD_PARAM_HDR_BUF)
	ld	bc,(ix + SLIPD_PARAM_HDR_LEN)

	; Phase 1: Sync to frame - discard until SLIP_END
	;
	; The per byte timeout is no protection here: a port that always claims
	; to have a byte ready never times out, so without a bound on what may
	; be discarded these two loops run for ever. That is not hypothetical.
	; A cartridge whose IO window is not answering reads as FF, which is
	; both "data available" and a byte that is not SLIP_END. IY is not
	; counting anything yet, so it counts the rubbish.
slipd_sync:
	SLIPD_WAIT_CHAR	slipd_done
	cp	SLIP_END
	jr	z, slipd_skip_end
	inc	iy
	push	iy
	pop	hl
	ld	a, h
	cp	SLIPD_JUNK_MAX
	jr	c, slipd_sync
	jp	slipd_done		; nothing that looks like a frame

	; Phase 2: Skip additional SLIP_END bytes
slipd_skip_end:
	SLIPD_WAIT_CHAR slipd_done
	cp	SLIP_END
	jr	nz, slipd_decode_start
	inc	iy
	push	iy
	pop	hl
	ld	a, h
	cp	SLIPD_JUNK_MAX
	jr	c, slipd_skip_end
	jp	slipd_done		; an unbroken run of frame markers

	; Phase 3: Decode - A has first data byte
slipd_decode_start:
	ld	iy, 0			; from here IY counts bytes written
slipd_decode_loop:
	cp	SLIP_END		; End of frame?
	jr	z, slipd_done

	cp	SLIP_ESC		; Escape prefix?
	jr	z, slipd_handle_escape

slipd_store_byte:
	; Write byte to current buffer (DE). Page 2 currently holds the IO
	; window, so give it back to RAM for the store and take it again
	; afterwards. HL is reloaded with the timeout before every read, so L
	; is free to hold the byte across the switch.
	ld	l, a
	ld	a, (ix + SLIPD_PARAM_SLOTS)		; page 2 = caller's RAM
	out	(SLOT_PORT), a
	ld	a, l
	ld	(de), a
	ld	a, (ix + SLIPD_PARAM_SLOTS + 1)		; page 2 = IO window
	out	(SLOT_PORT), a
	inc	de
	dec	bc
	inc	iy
	ld	a, b
	or	c
	jr	nz, slipd_read_next	; Buffer not yet full

	; Current buffer exhausted - check if we need to switch to data buffer
	ld	bc, (ix + SLIPD_PARAM_DATA_LEN)
	ld	a, b
	or	c
	jr	z, slipd_done

	ld	de, (ix + SLIPD_PARAM_DATA_BUF)

	; Zero out data_len so exhausting the data buffer ends the loop
	ld	(ix + SLIPD_PARAM_DATA_LEN), 0
	ld	(ix + SLIPD_PARAM_DATA_LEN+1), 0

slipd_read_next:
	SLIPD_WAIT_CHAR slipd_done
	jr	slipd_decode_loop

slipd_handle_escape:
	SLIPD_WAIT_CHAR slipd_done	; Read byte after ESC

	cp	SLIP_ESC_END		; 0xDC -> 0xC0
	jr	nz, slipd_check_esc_esc
	ld	a, SLIP_END
	jr	slipd_store_byte

slipd_check_esc_esc:
	cp	SLIP_ESC_ESC		; 0xDD -> 0xDB
	jr	nz, slipd_store_byte	; Unknown escape -- store as-is
	ld	a, SLIP_ESC
	jr	slipd_store_byte

slipd_done:
	push	iy			; Get total bytes written
	pop	de			; temp save into DE so we can return it in HL

	pop	iy			; Restore IY
	pop	ix			; Restore IX

	pop	bc			; BC = Return Address

	; Clean the arguments off the stack
	ld	hl, ARG_BYTE_LEN
	add	hl, sp
	ld	sp, hl         		; SP is now cleaned

	push	bc			; Put return address back
	ld	hl, de			; Return bytes written
	ret
