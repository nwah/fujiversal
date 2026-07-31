	INCLUDE	"page2.inc"
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
;                                uint16_t timeout)
;
; Read and decode a SLIP-framed packet into two buffers.
; (__z88dk_callee convention -- callee cleans the stack)
;
; Both buffers are written directly. Neither can be in page 2, because this
; ROM is paged there for the whole call, and the UNAPI specification says so.
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
; Stack on entry (right-to-left push, leftmost param nearest SP):
;   (SP+0)  = return address
;   (SP+2)  = hdr_buf          (leftmost, pushed last)
;   (SP+4)  = hdr_len
;   (SP+6)  = data_buf
;   (SP+8)  = data_len
;   (SP+10) = timeout          (rightmost, pushed first)
;
; Convert ms to jiffies before calling:
;   PAL  (50 Hz): jiffies = ms / 20
;   NTSC (60 Hz): jiffies = ms / 17  (approx)
;
; Returns:
;   HL = total decoded bytes written (header + data)
;-----------------------------------------------------------------------------

	ARG_BYTE_LEN	equ	10	; 5 words

	; How much may be thrown away before a frame starts, in units of 256
	; bytes. Comfortably more than one packet, so a late reply still syncs.
	SLIPD_JUNK_MAX	equ	8

	SLIPD_PARAM_HDR_BUF	equ	6
	SLIPD_PARAM_HDR_LEN	equ	8
	SLIPD_PARAM_DATA_BUF	equ	10
	SLIPD_PARAM_DATA_LEN	equ	12
	SLIPD_PARAM_TIMEOUT	equ	14

_port_getbuf_slip_dual:
	push	ix			; Callee-save IX
	push	iy			; Callee-save IY

	; Stack at this point:
	;   (SP+0)  = saved IY
	;   (SP+2)  = saved IX
	;   (SP+4)  = Return Address
	;   (SP+6)  = hdr_buf
	;   (SP+8)  = hdr_len
	;   (SP+10) = data_buf
	;   (SP+12) = data_len
	;   (SP+14) = timeout
	;
	; which is where the SLIPD_PARAM_ offsets above come from.

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
	; Write byte to current buffer (DE)
	ld	(de), a
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
