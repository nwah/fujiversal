#ifndef DISK_H
#define DISK_H

#include "fujinet.h"
#include <stdint.h>

#define SECTOR_SIZE     512

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

/* DSKFMT reuses the codes above up to 10, then parts company with them */
enum {
  FMT_ERR_BAD_PARAM = 12,
  FMT_ERR_NO_MEMORY = 14,
  FMT_ERR_OTHER     = 16,
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

/* Driver state. The kernel allocates this for us at boot -- MYSIZE in
   driver.asm says how much, and GETWRK hands back the address -- so it lands
   wherever the kernel keeps its own work areas, in page 3.

   There is deliberately no sector buffer here. Transfers go straight to the
   caller's memory, and the SLIP routines hand page 2 back for the instant it
   takes to touch it; DSKFMT builds its sectors in the work area the kernel
   passes in. Keep MYSIZE in driver.asm in step with this. */
#define MOUNT_STAMP_BYTES 4     /* low half of the FujiNet 64 bit mount time */

typedef struct {
  /* Mount time seen at the last media check, as raw bytes: comparing these
     as uint32_t would drag the 32 bit runtime helpers into a ROM that has no
     room for them. */
  uint8_t mount_time[FN_MAX_DEV][MOUNT_STAMP_BYTES];
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
