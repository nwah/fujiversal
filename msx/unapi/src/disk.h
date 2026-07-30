#ifndef DISK_H
#define DISK_H

#include "fujinet.h"
#include <stdint.h>

#define SECTOR_SIZE     512

/* Keep in sync with disk.inc */
#define DISK_STACK_SIZE 288
#define DISK_WORK_SIZE  (32 + DISK_STACK_SIZE)

/* Error codes returned in A alongside a set carry flag. Note that zero is
   a real error and not a success value. */
enum {
  DISK_ERR_WRITE_PROTECT = 0,
  DISK_ERR_NOT_READY     = 2,
  DISK_ERR_DATA          = 4,
  DISK_ERR_SEEK          = 6,
  DISK_ERR_NOT_FOUND     = 8,
  DISK_ERR_WRITE_FAULT   = 10,
  DISK_ERR_OTHER         = 12,
};

/* DSKCHG reports its result in B */
enum {
  DISK_CHANGED   = 0xFF,
  DISK_UNKNOWN   = 0,
  DISK_UNCHANGED = 1,
};

/* The registers the jump table entry stubs push on the way in, in the order
   a Z80 pushes them. The stubs pop this block straight back into the CPU on
   the way out, so a routine returns a value by writing to the block. */
typedef struct {
  uint8_t  f;                   /* flags; bit 0 is carry */
  uint8_t  a;
  uint8_t  c;
  uint8_t  b;
  uint16_t de;
  uint16_t hl;
} MsxRegs;

#define REG_CARRY       0x01

/* A register pair holding an address in the MSX's address space. Plain
   truncation on target; a host test harness redirects it into an arena
   standing in for the Z80's 64K. */
#ifndef MSX_PTR
#define MSX_PTR(value)  ((uint8_t *) (value))
#endif

/* Driver state, allocated below HIMEM by INIT and reached through the
   pointer INIT leaves in our SLTWRK entry. It has to live in page 3: a
   FujiNet transfer pages our own ROM over page 2, so anything the driver
   needs while a transfer is running cannot be there.

   Machines that ship built-in firmware are miserly with the memory below
   HIMEM -- a Panasonic FS-A1WX refuses to start its own software if a
   cartridge takes much beyond 400 bytes -- so this is kept as small as it
   will go. In particular there is deliberately no sector buffer: transfers
   go straight to the caller's memory, and the SLIP routines hand page 2
   back for the instant it takes to touch it. */
typedef struct {
  uint32_t mount_time[FN_MAX_DEV]; /* mount time seen at the last media check */

  /* Private stack, growing down from the end of the work area. It goes
     last so that an overflow eats our own state rather than running off
     the bottom into memory that belongs to BASIC. */
  uint8_t  stack[DISK_STACK_SIZE];
} DiskWork;

/* Implemented in disk_entry.s */
extern DiskWork *disk_get_work(void);

/* Called from the jump table stubs in disk_entry.s */
extern void __FASTCALL__ disk_io(MsxRegs *regs);
extern void __FASTCALL__ disk_chg(MsxRegs *regs);
extern void __FASTCALL__ disk_getdpb(MsxRegs *regs);
extern void __FASTCALL__ disk_choice(MsxRegs *regs);
extern void __FASTCALL__ disk_fmt(MsxRegs *regs);

#endif /* DISK_H */
