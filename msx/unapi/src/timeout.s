	public	timeout_init, timeout_check, timeout_cleanup

; ============================================================
; Timeout routines for the receive loop
; ------------------------------------------------------------
; These used to time their waits off JIFFY, which only the frame interrupt
; advances. That made the transport unusable with interrupts off, and turning
; them on for the length of a call is not something this ROM may do: a client
; reaches it through an inter-slot call, so while the call runs the client's
; own slot is out of page 1. An interrupt taken in that window runs the
; machine's interrupt path with whatever the client had there missing -- under
; Nextor that is the disk kernel, and the boot dies a few sectors in.
;
; So the wait is counted rather than clocked. TICK_LOOPS is how many times
; timeout_check is called in the time one frame interrupt would have taken,
; measured in the receive loop of port_getc_timeout, which is its only caller:
; a 3.58MHz Z80 gets through about 13,900 turns of that loop a second, so 232
; of them is a 60Hz frame. Timeouts keep the units the callers already use,
; and a faster CPU simply makes them shorter in real time.
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
	inc	sp		; discard a byte
	inc	sp		; discard a byte
	inc	sp		; discard a byte
	inc	sp		; discard a byte
	push	af		; restore return address
	ret
