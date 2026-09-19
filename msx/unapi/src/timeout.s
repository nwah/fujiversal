	public	timeout_init, timeout_check, timeout_cleanup

; ============================================================
; Timeout routines for the receive loop
; ------------------------------------------------------------
; These used to time off JIFFY, which only the frame interrupt advances --
; unusable with interrupts off, and this ROM can't turn them on for a call:
; a client reaches it through an inter-slot call, so its own slot is out of
; page 1 while the call runs, and an interrupt taken then runs with whatever
; the client had there missing (under Nextor, the disk kernel -- boot dies a
; few sectors in).
;
; So the wait is counted instead. TICK_LOOPS is how many timeout_check calls
; fit in one frame interrupt's time, measured in port_getc_timeout's receive
; loop (its only caller): ~13,900 loops/sec on a 3.58MHz Z80, so 232 per 60Hz
; frame. Units stay what callers already use; a faster CPU just shortens the
; real time.
;
; timeout_init    - HL = timeout duration (in frame times)
;                   pushes the timeout and a fresh loop count on the stack
; timeout_check   - sets Carry if the timeout has elapsed
; timeout_cleanup - pops the saved values from the stack
; ============================================================

	TICK_LOOPS	EQU	240

; ------------------------------------------------------------
; timeout_init
;   Input: HL = timeout duration (16-bit)
;   Stack: pushes the timeout and the loop count
;   Destroys: HL, BC
; ------------------------------------------------------------
timeout_init:
	pop	bc		; save return address
	push	hl		; push TIMEOUT
	ld	hl,TICK_LOOPS
	push	hl		; push COUNT on stack
	push	bc		; restore return address
	ret

; ------------------------------------------------------------
; timeout_check
;   Counts one turn of the caller's loop off the timeout.
;   Sets Carry if the timeout has elapsed.
;   Destroys: A, BC, DE, HL
; ------------------------------------------------------------
timeout_check:
	pop	hl		; save return address
	pop	de		; DE = COUNT
	pop	bc		; BC = TIMEOUT

	dec	de
	ld	a,d
	or	e
	jr	nz,timeout_running

	ld	de,TICK_LOOPS	; one whole frame time gone
	dec	bc
	ld	a,b
	or	c
	jr	z,timeout_elapsed

timeout_running:
	push	bc
	push	de
	push	hl
	or	a		; Cy = 0: still waiting
	ret

timeout_elapsed:
	push	bc
	push	de
	push	hl
	scf
	ret

; ------------------------------------------------------------
; timeout_cleanup
;   Discards COUNT and TIMEOUT from top of stack
;   Destroys: AF
; ------------------------------------------------------------
timeout_cleanup:
	pop	af		; save return address
	inc	sp		; discard COUNT and TIMEOUT (4 bytes)
	inc	sp
	inc	sp
	inc	sp
	push	af		; restore return address
	ret
