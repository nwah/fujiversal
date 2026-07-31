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
#include "unapi_call.h"
#include <string.h>

/* Fails to compile if DiskWork outgrows the MYSIZE the kernel is told to
   allocate. Keep the two in step; MYSIZE lives in driver.asm. */
#define MYSIZE  536
typedef char disk_work_size_check[sizeof(DiskWork) <= MYSIZE ? 1 : -1];

/* Where the BPB starts inside a FAT boot sector, and the offsets within it.
   Nothing reads these any more -- DSKFMT copies a ready made block in -- but
   they are what names the columns of the bpb rows in formats[] below. */
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

/* Offsets within a dpb_table row, which is a DPB without its drive number.
   Like the BPB offsets above these name the columns rather than being read:
   GETDPB copies a whole row into the block the kernel hands it. */
#define ROW_MEDIA       0
#define ROW_CLUSMASK    5
#define ROW_MAXENT      10
#define ROW_FIRREC      11
#define ROW_MAXCLUS     13
#define ROW_FATSIZ      15

/* Sectors on each of those formats, in the same order. Each one is what the
   row above works out to: first data sector + (highest cluster - 1) sectors
   per cluster. Held as a table because the multiply to derive it costs more
   than the sixteen bytes it would save. */
static const uint16_t media_sectors[] = {
  /* F8 */  720,   /* F9 */ 1440,  /* FA */  640,  /* FB */ 1280,
  /* FC */  360,   /* FD */  720,  /* FE */  320,  /* FF */  640,
};

/* What CHOICE offers and DSKFMT then formats: the two standard MSX shapes.
   Everything either of them needs is here, ready to copy into a boot sector,
   which keeps DSKFMT down to moving bytes about. Working the same numbers out
   of dpb_table would cost more code than the forty bytes this table is. */
#define FORMAT_CHOICES  2

typedef struct {
  uint8_t media;
  uint8_t fat_size;             /* sectors per FAT */
  uint8_t first_data;           /* first sector past the FATs and the root */
  uint8_t bpb[BPB_SIZE];        /* the block at BPB_OFFSET of a boot sector */
} MsxFormat;

static const MsxFormat formats[FORMAT_CHOICES] = {
  /* 360K, single sided */
  { 0xf8, 2, 12,
    { 0x00, 0x02, 0x02, 0x01, 0x00, 0x02, 0x70, 0x00, 0xd0, 0x02,
      0xf8, 0x02, 0x00, 0x09, 0x00, 0x01, 0x00 } },
  /* 720K, double sided */
  { 0xf9, 3, 14,
    { 0x00, 0x02, 0x02, 0x01, 0x00, 0x02, 0x70, 0x00, 0xa0, 0x05,
      0xf9, 0x03, 0x00, 0x09, 0x00, 0x02, 0x00 } },
};

static const char format_prompt[] = "1 - Single sided\r\n2 - Double sided\r\n";



/*
 * FujiNet calls
 *
 * Every one of them leaves through the UNAPI entry point in page 2. The
 * parameter block lives in the work area rather than on the stack because
 * the specification says it may not be in page 1 or page 2, and the stack
 * belongs to whoever called the kernel.
 */

static uint8_t disk_call(uint8_t device, uint8_t command, uint8_t aux_descr,
                         uint16_t aux_low, void *buffer, uint16_t length,
                         uint8_t read)
{
  FujiNetParams *params = &disk_get_work()->params;


  params->device = device;
  params->command = command;
  params->aux_descr = aux_descr;
  params->aux[0] = (uint8_t) aux_low;
  params->aux[1] = (uint8_t) (aux_low >> 8);
  params->aux[2] = 0;
  params->aux[3] = 0;
  params->buffer = buffer;
  params->length = length;

  return read ? unapi_fuji_read(params) : unapi_fuji_write(params);
}

/* An address the FujiNet call cannot reach, because this cartridge is sitting
   over it while the call runs: page 1 is ours already and page 2 becomes ours
   for the length of the call. Sectors bound for one get bounced through the
   work area, which is in page 3. */
#define UNREACHABLE(ptr)        (((uint16_t) (ptr)) >= 0x4000 && \
                                 ((uint16_t) (ptr)) < 0xC000)

static uint8_t disk_read_sector(uint8_t drive, uint16_t sector, uint8_t *buffer)
{
  DiskWork *work = disk_get_work();


  if (!UNREACHABLE(buffer))
    return disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_READ,
                     FUJI_FIELD_C1234, sector, buffer, SECTOR_SIZE, 1);

  if (!disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_READ, FUJI_FIELD_C1234,
                 sector, work->buffer, SECTOR_SIZE, 1))
    return 0;
  memcpy(buffer, work->buffer, SECTOR_SIZE);
  return 1;
}

static uint8_t disk_write_sector(uint8_t drive, uint16_t sector, uint8_t *buffer)
{
  DiskWork *work = disk_get_work();


  if (UNREACHABLE(buffer)) {
    memcpy(work->buffer, buffer, SECTOR_SIZE);
    buffer = work->buffer;
  }
  return disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_WRITE,
                   FUJI_FIELD_C1234, sector, buffer, SECTOR_SIZE, 0);
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
 * A transfer address in page 2 is the kernel's to give -- that is where the
 * TPA runs -- so it is bounced rather than refused.
 */
void __FASTCALL__ disk_io(MsxRegs *regs)
{
  uint8_t drive = regs->a;
  uint8_t count = regs->b;
  uint8_t write = regs->f & REG_CARRY;
  uint16_t sector = regs->de;
  uint8_t *buffer = MSX_PTR(regs->hl);
  uint8_t err = DISK_ERR_OTHER;
  int8_t row = media_row(regs->c);
  uint16_t limit;


  if (drive >= FN_DRIVES)
    goto fail;

  /* Refuse a transfer that would run off the end of a disk of the shape the
     kernel says this is. A media descriptor we do not recognise describes no
     particular shape, so there is nothing to check it against. */
  if (row >= 0) {
    limit = media_sectors[row];
    if (sector >= limit || (uint16_t) (limit - sector) < count) {
      err = DISK_ERR_NOT_FOUND;
      goto fail;
    }
  }

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
  /* The reply covers every slot at once. It is staged in the work area
     because a FujiNet call cannot write anywhere else this driver can put
     it: the stack is the caller's and may be in page 2. */
  uint8_t *reply = work->buffer;


  if (drive >= FN_DRIVES) {
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
  regs->hl = (uint16_t) format_prompt;
  return;
}

/*
 * DSKFMT (401Ch): lay a fresh filesystem on the image.
 *
 * In:  A = choice, from 1, D = drive, HL = a work area, BC = its length
 * Out: Cy = 0 on success, else Cy = 1 and A = error code
 *
 * There is no medium here to lay tracks on, so formatting is only writing a
 * boot sector, two empty FATs and an empty root directory. The kernel offers
 * a work area to build them in, but this driver has its own in page 3 and a
 * FujiNet call cannot be handed anything else, so the kernel's is left alone.
 *
 * The image itself is whatever size the FujiNet mounted; nothing here can
 * change that. Formatting a slot holding an image of some other shape will
 * write a filesystem that claims a size the image does not have.
 */
void __FASTCALL__ disk_fmt(MsxRegs *regs)
{
  uint8_t *sector = disk_get_work()->buffer;
  uint8_t choice = regs->a;
  uint8_t drive = (uint8_t) (regs->de >> 8);
  const MsxFormat *fmt;
  uint16_t idx;


  if (drive >= FN_DRIVES || choice < 1 || choice > FORMAT_CHOICES) {
    regs->a = FMT_ERR_BAD_PARAM;
    regs->f |= REG_CARRY;
    return;
  }
  fmt = &formats[choice - 1];

  /* The boot sector. The jump at the front is the usual one to itself: this
     disk holds no boot code, and everything that matters follows it. */
  memset(sector, 0, SECTOR_SIZE);
  sector[0] = 0xeb;
  sector[1] = 0xfe;
  sector[2] = 0x90;
  memcpy(&sector[BPB_OFFSET], fmt->bpb, BPB_SIZE);
  if (!disk_write_sector(drive, 0, sector))
    goto fail;

  /* Both FATs and the whole root directory start empty */
  memset(sector, 0, SECTOR_SIZE);
  for (idx = 1; idx < fmt->first_data; idx++)
    if (!disk_write_sector(drive, idx, sector))
      goto fail;

  /* except that each FAT opens with the two reserved entries */
  sector[0] = fmt->media;
  sector[1] = 0xff;
  sector[2] = 0xff;
  if (!disk_write_sector(drive, 1, sector))
    goto fail;
  if (!disk_write_sector(drive, 1 + fmt->fat_size, sector))
    goto fail;

  regs->f &= ~REG_CARRY;
  return;

 fail:
  /* A slot mounted read only refuses every one of these writes */
  regs->a = DISK_ERR_WRITE_PROTECT;
  regs->f |= REG_CARRY;
  return;
}
