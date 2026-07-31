/*
 * FujiNet MSX disk driver.
 *
 * Implements the routines the MSX-DOS kernel reaches through the disk jump
 * table at 4010h, backing each drive with a FujiNet disk device slot. Every
 * FujiNet call goes out through UNAPI_ENTRY, see unapi_call.s.
 *
 * The register level entry points live in disk_entry.s; by the time control
 * arrives here the incoming registers are a MsxRegs block and the code is
 * running on the driver's own stack.
 */

#include "disk.h"
#include "fujinet.h"
#include "fuji_call.h"
#include <string.h>

/* Fails to compile if DiskWork outgrows the MYSIZE the kernel is told to
   allocate. Keep the two in step; MYSIZE lives in driver.asm. */
#define MYSIZE  32
typedef char disk_work_size_check[sizeof(DiskWork) <= MYSIZE ? 1 : -1];

/* Where the BPB starts inside a FAT boot sector, and the offsets within it */
#define BPB_OFFSET      0x0b
#define BPB_SECSIZE     0       /* word: bytes per sector */
#define BPB_CLUSSIZE    2       /* byte: sectors per cluster */
#define BPB_RESERVED    3       /* word: reserved sectors before the first FAT */
#define BPB_FATCNT      5       /* byte: number of FATs */
#define BPB_DIRENT      6       /* word: root directory entries */
#define BPB_TOTSEC      8       /* word: total sectors */
#define BPB_MEDIA       10      /* byte: media descriptor */
#define BPB_FATSIZE     11      /* word: sectors per FAT */
#define BPB_SECPERTRK   13      /* word: sectors per track */
#define BPB_HEADS       15      /* word: heads */
#define BPB_SIZE        17

/* Offsets within the Drive Parameter Block the kernel hands us. Written
   through a byte pointer because the layout is not naturally aligned. */
#define DPB_MEDIA       1       /* byte: media descriptor */
#define DPB_SECSIZE     2       /* word: bytes per sector */
#define DPB_DIRMASK     4       /* byte: directory entries per sector - 1 */
#define DPB_DIRSHFT     5       /* byte: bits set in DPB_DIRMASK */
#define DPB_CLUSMASK    6       /* byte: sectors per cluster - 1 */
#define DPB_CLUSSHFT    7       /* byte: bits set in DPB_CLUSMASK, plus one */
#define DPB_FIRFAT      8       /* word: first FAT sector */
#define DPB_FATCNT      10      /* byte: number of FATs */
#define DPB_MAXENT      11      /* byte: root directory entries, capped at 254 */
#define DPB_FIRREC      12      /* word: first data sector */
#define DPB_MAXCLUS     14      /* word: highest cluster number */
#define DPB_FATSIZ      16      /* byte: sectors per FAT */
#define DPB_FIRDIR      17      /* word: first root directory sector */

#define DIR_ENTRY_SIZE  32
#define DIR_PER_SECTOR  (SECTOR_SIZE / DIR_ENTRY_SIZE)

/* Geometry of the standard MSX formats, indexed by media descriptor - 0xF8.
   All of them reserve one boot sector and carry two FATs. */
/* The 19 byte Drive Parameter Block the kernel wants, minus its leading drive
   number, held ready made for each of the eight standard MSX formats. These
   used to be computed from a BPB at run time; the arithmetic needed a 16 bit
   divide and cost far more code than the 144 bytes of table it produced, and
   this ROM has only the tail of page 1 to live in. Generated from the same
   formula the host test harness still checks. */

#define DPB_BYTES       18
#define MEDIA_FIRST     0xf8
#define MEDIA_LAST      0xff
#define MEDIA_DEFAULT   0xf9    /* double sided 720K, the usual MSX disk */

static const uint8_t dpb_table[][DPB_BYTES] = {
  /* F8 */ { 0xf8, 0x00, 0x02, 0x0f, 0x04, 0x01, 0x02, 0x01, 0x00, 0x02, 0x70, 0x0c, 0x00, 0x63, 0x01, 0x02, 0x05, 0x00 },
  /* F9 */ { 0xf9, 0x00, 0x02, 0x0f, 0x04, 0x01, 0x02, 0x01, 0x00, 0x02, 0x70, 0x0e, 0x00, 0xca, 0x02, 0x03, 0x07, 0x00 },
  /* FA */ { 0xfa, 0x00, 0x02, 0x0f, 0x04, 0x00, 0x01, 0x01, 0x00, 0x02, 0x70, 0x0c, 0x00, 0x75, 0x02, 0x02, 0x05, 0x00 },
  /* FB */ { 0xfb, 0x00, 0x02, 0x0f, 0x04, 0x01, 0x02, 0x01, 0x00, 0x02, 0x70, 0x0e, 0x00, 0x7a, 0x02, 0x03, 0x07, 0x00 },
  /* FC */ { 0xfc, 0x00, 0x02, 0x0f, 0x04, 0x00, 0x01, 0x01, 0x00, 0x02, 0x40, 0x09, 0x00, 0x60, 0x01, 0x02, 0x05, 0x00 },
  /* FD */ { 0xfd, 0x00, 0x02, 0x0f, 0x04, 0x01, 0x02, 0x01, 0x00, 0x02, 0x70, 0x0c, 0x00, 0x63, 0x01, 0x02, 0x05, 0x00 },
  /* FE */ { 0xfe, 0x00, 0x02, 0x0f, 0x04, 0x00, 0x01, 0x01, 0x00, 0x02, 0x40, 0x07, 0x00, 0x3a, 0x01, 0x01, 0x03, 0x00 },
  /* FF */ { 0xff, 0x00, 0x02, 0x0f, 0x04, 0x01, 0x02, 0x01, 0x00, 0x02, 0x70, 0x0a, 0x00, 0x3c, 0x01, 0x01, 0x03, 0x00 },
};

/* Offsets within a dpb_table row */
#define ROW_MEDIA       0
#define ROW_MAXENT      10
#define ROW_FIRREC      11
#define ROW_MAXCLUS     13
#define ROW_FATSIZ      15



/*
 * FujiNet calls
 */

static uint8_t disk_call(uint8_t device, uint8_t command, uint8_t aux_descr,
                         uint16_t aux_low, void *buffer, uint16_t length,
                         uint8_t read)
{
  fujibus_packet packet;


  packet.header.device = device;
  packet.header.command = command;
  packet.header.fields = aux_descr;
  packet.data[0] = (uint8_t) aux_low;
  packet.data[1] = (uint8_t) (aux_low >> 8);
  packet.data[2] = 0;
  packet.data[3] = 0;

  return fuji_packet_call(read ? SIO_DIRECTION_READ : SIO_DIRECTION_WRITE,
                          &packet, buffer, length);
}

/* Sectors move straight between the FujiNet and the caller's memory. A
   transfer pages this ROM over page 2, so a buffer that lives there would
   normally be hidden for the duration; the SLIP routines hand page 2 back
   around each access to it, which is what makes this safe. */

static uint8_t disk_read_sector(uint8_t drive, uint16_t sector, void *buffer)
{
  return disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_READ, FUJI_FIELD_C1234,
                   sector, buffer, SECTOR_SIZE, 1);
}

static uint8_t disk_write_sector(uint8_t drive, uint16_t sector, const void *buffer)
{
  return disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_WRITE, FUJI_FIELD_C1234,
                   sector, (void *) buffer, SECTOR_SIZE, 0);
}

/*
 * Drive Parameter Block
 */

/* Index into dpb_table/media_sectors, or -1 for a descriptor we do not know */
static int8_t media_row(uint8_t media)
{
  if (media < MEDIA_FIRST || media > MEDIA_LAST)
    return -1;
  return (int8_t) (media - MEDIA_FIRST);
}

/*
 * Build a DPB for a drive from its media descriptor.
 *
 * This is what MSX-DOS 1 expects of GETDPB, which is why the kernel also
 * passes the first byte of the FAT: between them the descriptor names one
 * of the eight standard shapes, and each of those has exactly one DPB.
 * Reading the disk's own BPB would describe an odd sized image more
 * faithfully, but it would need somewhere to put the sector it read.
 */
static void disk_build_dpb(uint8_t *dpb, uint8_t media)
{
  int8_t row = media_row(media);


  if (row < 0)
    row = media_row(MEDIA_DEFAULT);
  memcpy(&dpb[DPB_MEDIA], dpb_table[row], DPB_BYTES);
}

/*
 * Jump table routines
 */

/*
 * DSKIO (4010h): transfer sectors.
 *
 * In:  Cy = 0 to read, 1 to write, A = drive, B = sector count,
 *      C = media descriptor, DE = first sector, HL = transfer address
 * Out: Cy = 0 on success. On failure Cy = 1, A = error code and
 *      B = the number of sectors still untransferred.
 *
 * The transfer address is used as it stands, including when it points into
 * page 2, which a FujiNet transfer would otherwise have paged this ROM over.
 */
void __FASTCALL__ disk_io(MsxRegs *regs)
{
  uint8_t drive = regs->a;
  uint8_t count = regs->b;
  uint8_t write = regs->f & REG_CARRY;
  uint16_t sector = regs->de;
  uint8_t *buffer = MSX_PTR(regs->hl);
  uint8_t err = DISK_ERR_OTHER;


  if (drive >= FN_MAX_DEV)
    goto fail;

  /* No bounds check on the sector number: the device rejects anything past
     the end of the image, and the table it would take to check against does
     not fit. */
  while (count) {
    if (write) {
      if (!disk_write_sector(drive, sector, buffer)) {
        /* Nothing distinguishes a device that refused the write from one
           that could not be reached, and a slot mounted read only is much
           the likelier of the two. */
        err = DISK_ERR_WRITE_PROTECT;
        goto fail;
      }
    }
    else {
      if (!disk_read_sector(drive, sector, buffer)) {
        err = DISK_ERR_NOT_READY;
        goto fail;
      }
    }

    buffer += SECTOR_SIZE;
    sector++;
    count--;
  }

  regs->b = 0;
  regs->f &= ~REG_CARRY;
  return;

 fail:
  regs->b = count;
  regs->a = err;
  regs->f |= REG_CARRY;
  return;
}

/*
 * DSKCHG (4013h): report whether the disk has been swapped.
 *
 * In:  A = drive, C = media descriptor, HL = DPB address
 * Out: Cy = 0 and B = 1 unchanged, 0 unknown, -1 changed. The DPB is
 *      rebuilt when the disk has changed.
 *
 * FujiNet stamps a mount time on each device slot, so a change is a change
 * in that stamp rather than anything the drive itself reports.
 */
void __FASTCALL__ disk_chg(MsxRegs *regs)
{
  DiskWork *work = disk_get_work();
  uint8_t drive = regs->a;
  const uint8_t *stamp;
  uint8_t idx, mounted;
  /* The reply covers every slot at once, so it is staged here rather than
     kept in the work area: the stack gives the space back afterwards. */
  uint8_t reply[MOUNT_TIME_REPLY_SIZE];


  if (drive >= FN_MAX_DEV) {
    regs->a = DISK_ERR_OTHER;
    regs->f |= REG_CARRY;
    return;
  }

  if (!disk_call(FUJI_DEVICEID_FUJINET, FUJICMD_STATUS, FUJI_FIELD_A1,
                 STATUS_MOUNT_TIME, reply, MOUNT_TIME_REPLY_SIZE, 1)) {
    regs->a = DISK_ERR_NOT_READY;
    regs->f |= REG_CARRY;
    return;
  }

  /* One 64 bit mount time per slot; the low half is all that can move in
     any timeframe worth worrying about. */
  stamp = &reply[drive * MOUNT_TIME_ENTRY];

  for (idx = 0, mounted = 0; idx < MOUNT_STAMP_BYTES; idx++)
    mounted |= stamp[idx];

  if (!mounted)
    regs->b = DISK_UNKNOWN;     /* nothing mounted in this slot */
  else if (memcmp(work->mount_time[drive], stamp, MOUNT_STAMP_BYTES)) {
    memcpy(work->mount_time[drive], stamp, MOUNT_STAMP_BYTES);
    regs->b = DISK_CHANGED;
    disk_build_dpb(MSX_PTR(regs->hl), regs->c);
  }
  else
    regs->b = DISK_UNCHANGED;

  regs->f &= ~REG_CARRY;
  return;
}

/*
 * GETDPB (4016h): describe the disk currently in a drive.
 *
 * In:  A = drive, B = first byte of the FAT, C = media descriptor,
 *      HL = DPB address
 * Out: the DPB is filled in from HL+1. There is no error path, so a drive
 *      that cannot be read still gets a plausible DPB.
 */
void __FASTCALL__ disk_getdpb(MsxRegs *regs)
{
  disk_build_dpb(MSX_PTR(regs->hl), regs->c);
  return;
}

/*
 * CHOICE (4019h): the format options DSKFMT accepts.
 *
 * Out: HL = a zero terminated string, or 0 for no choice at all.
 */
void __FASTCALL__ disk_choice(MsxRegs *regs)
{
  regs->hl = 0;             /* no choices: DSKFMT is not supported */
  return;
}

/*
 * DSKFMT (401Ch): not supported.
 *
 * Out: Cy = 1 and A = error code
 *
 * There is no medium to lay tracks on, so this would only mean writing a
 * boot sector and blank FATs -- but the code to do it does not fit. The
 * Disk BIOS fills most of this 16K ROM and what is left has to cover the
 * FujiNet transport first. Create images on the host instead, where the
 * FujiNet can make them properly sized.
 *
 * CHOICE reports no format choices, so the kernel should not offer this.
 */
void __FASTCALL__ disk_fmt(MsxRegs *regs)
{
  regs->a = FMT_ERR_OTHER;
  regs->f |= REG_CARRY;
  return;
}
