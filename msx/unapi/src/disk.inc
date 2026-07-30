;--- FujiNet MSX disk driver
;    Layout constants shared with disk.h. disk.c has a compile time check
;    that DiskWork still fits in DISK_WORK_SIZE, so keep the two in sync.

;--- Private stack the driver runs its C code on. The MSX-DOS kernel only
;    guarantees a small stack, and an interrupt can land on top of it.
;    It sits at the end of the work area, so the stack pointer starts at
;    DISK_WORK_SIZE and grows down through the driver's own state.

DISK_STACK_SIZE:  equ  288

;--- Total size of DiskWork: 8 mount times, then the stack. Machines with
;    built-in firmware are tight on memory below HIMEM, so this stays small;
;    a Panasonic FS-A1WX will not start its own software if a cartridge takes
;    much beyond 400 bytes.

DISK_WORK_SIZE:   equ  32 + DISK_STACK_SIZE

;--- Offset of the work area pointer within our 8 byte SLTWRK entry.
;    Bytes 0 to 4 hold the previous EXTBIO hook, see init.s.

DISK_WORK_SLOT:   equ  5

;--- Offset of the "work area has been cleared" marker in the same entry, and
;    the value that says so. INIT cannot clear the area itself, see init.s.

DISK_READY_SLOT:  equ  7
DISK_READY_MARK:  equ  0A5h

;--- Number of drives this interface provides, one per FujiNet device slot

FN_MAX_DEV:       equ  8

;--- "Other error", the MSX disk error code for anything unclassified

DISK_ERR_OTHER:   equ  12
