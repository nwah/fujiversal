;--- subslot.s: Subslot register access for the UNAPI ROM's IO window ---
;
; The IO window at 0xBFFCh lives in page 2 of the subslot this ROM is in.
; When something else occupies another subslot of the same primary slot,
; the BIOS may leave page 2 pointing at it, hiding the window from the
; transport.  These routines temporarily select our own subslot for page 2.
;
; Which subslot that is comes out of the subslot register itself rather than
; being assumed: while a call runs this ROM is what page 1 holds, so the
; register's page 1 field names our subslot.  The adapter puts us in subslot
; 0 whenever the slot is expanded, but nothing here depends on that -- an
; emulator or a board that lands us elsewhere works the same.
;
; Accessing the subslot register at 0xFFFF requires page 3 to be mapped
; to the fujiveral: the register is memory-mapped (not an I/O port), and
; the firmware only decodes it when page 3's primary slot is this one.
; Page 3 normally holds the stack, so we disable interrupts and switch
; page 3 only briefly — no stack operations occur in that window.  The
; interrupt-flag state is preserved with LD A,I / PUSH AF / DI … POP AF
; / conditional EI.  PUSH AF and POP AF straddle the page-3 switch but
; never land inside it, so the stack itself is never touched while page 3
; is remapped.  For the same reason the switch is written out in both
; routines rather than factored into one: a CALL to it could return, but
; its RET could not, because the return address is on a stack that is not
; there any more.
;
; Nothing here is kept in memory.  A `defb` in this file lands in the ROM,
; where a store is silently dropped and the read-back is whatever was
; assembled -- so the primary slot register stays in D for the few
; instructions that need it, and the one value that has to outlive
; io_window_enter (the subslot register as it was found) is handed back to
; the caller to pass to io_window_exit.  Neither routine needs a "did I
; change anything" flag: both work it out from EXPTBL, so every byte value
; the subslot register can hold round-trips unambiguously.

		INCLUDE	"const.inc"

		PUBLIC	_io_window_enter, _io_window_exit

;-----------------------------------------------------------------------------
; uint8_t io_window_enter(void)
;
;   Entry:  nothing on stack, interrupts as the caller left them.
;   Exit:   L = the subslot register as it was found, to be handed to
;               io_window_exit.  0 if the slot is not expanded, which
;               io_window_exit ignores -- it re-tests EXPTBL itself.
;   Destroys: A, BC, DE, HL, flags.
;
;   If the current slot is expanded, sets page 2's subslot to our own so
;   the IO window at 0xBFFCh is decoded.
;-----------------------------------------------------------------------------

_io_window_enter:
	in	a,(0A8h)
	ld	d,a			; D = primary slot register, for the
					; page-3 switch and its undo

	call	page1_expanded		; C = page 1's primary slot, Z if plain
	jr	z,enter_quiet

	; --- Expanded: manipulate the subslot register at 0xFFFF ---
	; Save interrupt state on the CURRENT stack (page 3 = main RAM),
	; then disable interrupts before switching page 3.  The push/pop
	; straddle the page-3 switch but never land inside it.
	ld	a,i			; P/V = IFF2
	push	af			; save interrupt state (stack is still main RAM)
	di

	ld	a,c			; page 3 -> our primary slot.  C is 0-3,
	rrca				; so rotating right twice lands it in
	rrca				; bits 7:6
	and	11000000b
	ld	b,a
	ld	a,d
	and	00111111b		; keep pages 0-2 as they were
	or	b
	out	(0A8h),a		; page 3 now = fujiveral

	ld	a,(0FFFFh)		; A = ~subslot_reg
	cpl				; A = subslot_reg
	ld	e,a			; E = it, as found -- the return value

	and	00001100b		; page 1's field: the subslot we are in,
	add	a,a			; since page 1 is this ROM while a call
	add	a,a			; runs.  Shift it into page 2's field
	ld	b,a
	ld	a,e
	and	11001111b		; clear page 2's field...
	or	b			; ...and point it at us
	ld	(0FFFFh),a

	ld	a,d
	out	(0A8h),a		; page 3 back to main RAM

	; Restore interrupt state (stack now in main RAM again)
	pop	af			; P/V = original IFF2
	jp	po,enter_no_ei		; jump if interrupts were disabled
	ei
enter_no_ei:

	ld	l,e
	ret

	; --- Not expanded: no-op ---
enter_quiet:
	ld	l,0
	ret

;-----------------------------------------------------------------------------
; void io_window_exit(uint8_t saved)
;   __FASTCALL__ (saved passed in L)
;
;   Entry:  L = the value io_window_enter returned.
;   Exit:   The subslot register is back to what enter found.  A slot that
;           is not expanded was never touched, and is left alone.
;   Destroys: A, BC, DE, HL, flags.
;-----------------------------------------------------------------------------

_io_window_exit:
	ld	e,l			; E = subslot register to write back

	in	a,(0A8h)
	ld	d,a

	call	page1_expanded
	ret	z			; not expanded: enter changed nothing

	ld	a,i
	push	af
	di

	ld	a,c			; page 3 -> our primary slot
	rrca
	rrca
	and	11000000b
	ld	b,a
	ld	a,d
	and	00111111b
	or	b
	out	(0A8h),a

	ld	a,e
	ld	(0FFFFh),a		; put the subslot register back

	ld	a,d
	out	(0A8h),a		; page 3 back to main RAM

	pop	af
	jp	po,exit_no_ei
	ei
exit_no_ei:

	ret

;-----------------------------------------------------------------------------
; page1_expanded: C = the primary slot connected to page 1, Z set if that
;   slot is not expanded.  D holds the primary slot register on entry.
;   Called only while page 3 still holds the stack.
;   Destroys: A, BC, HL, flags.
;-----------------------------------------------------------------------------

page1_expanded:
	ld	a,d
	and	00001100b		; page 1's bits of the slot register
	rrca
	rrca
	ld	c,a			; C = primary slot for page 1
	ld	b,0
	ld	hl,EXPTBL		; 0xFCC1
	add	hl,bc
	bit	7,(hl)			; Z if not expanded
	ret
