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

/* Fails to compile if DiskWork outgrows the space init.s reserves */
typedef char disk_work_size_check[sizeof(DiskWork) <= DISK_WORK_SIZE ? 1 : -1];

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
typedef struct {
  uint8_t  cluster_size;
  uint8_t  fat_size;
  uint16_t dir_entries;
  uint16_t total;
  uint8_t  sectors_per_track;
  uint8_t  heads;
} MsxFormat;

static const MsxFormat msx_formats[] = {
  /* F8 */ { 2, 2, 112,  720, 9, 1 },
  /* F9 */ { 2, 3, 112, 1440, 9, 2 },
  /* FA */ { 1, 2, 112,  640, 8, 1 },
  /* FB */ { 2, 3, 112, 1280, 8, 2 },
  /* FC */ { 1, 2,  64,  360, 9, 1 },
  /* FD */ { 2, 2, 112,  720, 9, 2 },
  /* FE */ { 1, 1,  64,  320, 8, 1 },
  /* FF */ { 2, 1, 112,  640, 8, 2 },
};

#define MEDIA_FIRST     0xf8
#define MEDIA_DEFAULT   0xf9    /* double sided 720K, the usual MSX disk */

/* The formats DSKFMT offers, in the order CHOICE lists them */
static const uint8_t format_choices[] = { 0xf8, 0xf9 };

static const char choice_text[] =
  "1 - Single sided 360K\r\n"
  "2 - Double sided 720K\r\n";

/*
 * Sector images DSKFMT writes.
 *
 * These are whole sectors held in ROM rather than composed in RAM, because
 * the driver has no sector buffer to compose them in: every byte of RAM it
 * reserves is a byte taken away from the machine's own software. A FujiNet
 * write has to hand over all 512 bytes at once, so a partial template is no
 * use, and ROM is the one thing there is plenty of.
 */

/* A boot sector with no boot code. MSX-DOS only needs the leading jump and
   a BPB to recognise the disk, and the rest of the sector reads as zero. */
static const uint8_t boot_360k[SECTOR_SIZE] = {
  0xeb, 0xfe, 0x90,                     /* JR $ ; NOP */
  'F', 'U', 'J', 'I', 'N', 'E', 'T', ' ',
  /* BPB, at offset 0x0b */
  0x00, 0x02,                           /* 512 bytes per sector */
  0x02,                                 /* sectors per cluster */
  0x01, 0x00,                           /* reserved sectors */
  0x02,                                 /* FAT copies */
  0x70, 0x00,                           /* 112 root directory entries */
  0xd0, 0x02,                           /* 720 sectors */
  0xf8,                                 /* media descriptor */
  0x02, 0x00,                           /* sectors per FAT */
  0x09, 0x00,                           /* sectors per track */
  0x01, 0x00,                           /* heads */
};

static const uint8_t boot_720k[SECTOR_SIZE] = {
  0xeb, 0xfe, 0x90,
  'F', 'U', 'J', 'I', 'N', 'E', 'T', ' ',
  0x00, 0x02,
  0x02,
  0x01, 0x00,
  0x02,
  0x70, 0x00,
  0xa0, 0x05,                           /* 1440 sectors */
  0xf9,
  0x03, 0x00,
  0x09, 0x00,
  0x02, 0x00,
};

/* The head of a FAT: the media descriptor and two set bytes, standing in
   for the reserved entries for clusters 0 and 1. */
static const uint8_t fat_head_360k[SECTOR_SIZE] = { 0xf8, 0xff, 0xff };
static const uint8_t fat_head_720k[SECTOR_SIZE] = { 0xf9, 0xff, 0xff };

/* Everything else a fresh filesystem needs is empty */
static const uint8_t empty_sector[SECTOR_SIZE] = { 0 };

/*
 * Little endian word access. The BPB and DPB both place words at odd
 * offsets, so they are picked apart a byte at a time.
 */

static uint16_t peek16(const uint8_t *buf, uint8_t offset)
{
  return buf[offset] | ((uint16_t) buf[offset + 1] << 8);
}

static uint32_t peek32(const uint8_t *buf, uint8_t offset)
{
  return (uint32_t) peek16(buf, offset)
    | ((uint32_t) peek16(buf, offset + 2) << 16);
}

static void poke16(uint8_t *buf, uint8_t offset, uint16_t value)
{
  buf[offset] = (uint8_t) value;
  buf[offset + 1] = (uint8_t) (value >> 8);
}

/* Number of low bits set, which for a power-of-two mask is log2(mask + 1) */
static uint8_t bit_count(uint8_t mask)
{
  uint8_t count;


  for (count = 0; mask; mask >>= 1)
    count++;
  return count;
}

/*
 * FujiNet calls
 */

static uint8_t disk_call(uint8_t device, uint8_t command, uint8_t aux_descr,
                         uint16_t aux_low, uint16_t aux_high,
                         void *buffer, uint16_t length, uint8_t read)
{
  FujiNetParams params;


  params.device = device;
  params.command = command;
  params.aux_descr = aux_descr;
  params.aux[0] = (uint8_t) aux_low;
  params.aux[1] = (uint8_t) (aux_low >> 8);
  params.aux[2] = (uint8_t) aux_high;
  params.aux[3] = (uint8_t) (aux_high >> 8);
  params.buffer = buffer;
  params.length = length;

  if (read)
    return unapi_fuji_read(&params);
  return unapi_fuji_write(&params);
}

/* Sectors move straight between the FujiNet and the caller's memory. A
   transfer pages this ROM over page 2, so a buffer that lives there would
   normally be hidden for the duration; the SLIP routines hand page 2 back
   around each access to it, which is what makes this safe. */

static uint8_t disk_read_sector(uint8_t drive, uint16_t sector, void *buffer)
{
  return disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_READ, FUJI_FIELD_C1234,
                   sector, 0, buffer, SECTOR_SIZE, 1);
}

static uint8_t disk_write_sector(uint8_t drive, uint16_t sector, const void *buffer)
{
  return disk_call(FUJI_DEVICEID_DISK + drive, FUJICMD_WRITE, FUJI_FIELD_C1234,
                   sector, 0, (void *) buffer, SECTOR_SIZE, 0);
}

/*
 * Drive Parameter Block
 */

/* Sectors on a disk of this shape, or zero if the descriptor names no
   shape we know. Used to keep a stray sector number off the wire. */
static uint16_t media_total(uint8_t media)
{
  if (media < MEDIA_FIRST)
    return 0;
  return msx_formats[media - MEDIA_FIRST].total;
}

static void disk_dpb_from_bpb(uint8_t *dpb, const uint8_t *bpb)
{
  uint16_t sector_size = peek16(bpb, BPB_SECSIZE);
  uint8_t cluster_size = bpb[BPB_CLUSSIZE];
  uint16_t reserved = peek16(bpb, BPB_RESERVED);
  uint8_t fat_count = bpb[BPB_FATCNT];
  uint16_t dir_entries = peek16(bpb, BPB_DIRENT);
  uint16_t total = peek16(bpb, BPB_TOTSEC);
  uint16_t fat_size = peek16(bpb, BPB_FATSIZE);
  uint16_t dir_per_sector = sector_size / DIR_ENTRY_SIZE;
  uint16_t first_dir, first_data;


  first_dir = reserved + (uint16_t) fat_count * fat_size;
  first_data = first_dir + (dir_entries + dir_per_sector - 1) / dir_per_sector;

  dpb[DPB_MEDIA] = bpb[BPB_MEDIA];
  poke16(dpb, DPB_SECSIZE, sector_size);
  dpb[DPB_DIRMASK] = (uint8_t) (dir_per_sector - 1);
  dpb[DPB_DIRSHFT] = bit_count(dpb[DPB_DIRMASK]);
  dpb[DPB_CLUSMASK] = cluster_size - 1;
  dpb[DPB_CLUSSHFT] = bit_count(dpb[DPB_CLUSMASK]) + 1;
  poke16(dpb, DPB_FIRFAT, reserved);
  dpb[DPB_FATCNT] = fat_count;
  dpb[DPB_MAXENT] = dir_entries > 254 ? 254 : (uint8_t) dir_entries;
  poke16(dpb, DPB_FIRREC, first_data);

  /* Clusters are numbered from two, so the highest cluster number is the
     number of data clusters plus one. */
  poke16(dpb, DPB_MAXCLUS, (total - first_data) / cluster_size + 1);

  dpb[DPB_FATSIZ] = (uint8_t) fat_size;
  poke16(dpb, DPB_FIRDIR, first_dir);
}

/* Fill in a BPB for one of the standard MSX formats */
static void bpb_from_media(uint8_t *bpb, uint8_t media)
{
  const MsxFormat *fmt = &msx_formats[media - MEDIA_FIRST];


  memset(bpb, 0, BPB_SIZE);
  poke16(bpb, BPB_SECSIZE, SECTOR_SIZE);
  bpb[BPB_CLUSSIZE] = fmt->cluster_size;
  poke16(bpb, BPB_RESERVED, 1);
  bpb[BPB_FATCNT] = 2;
  poke16(bpb, BPB_DIRENT, fmt->dir_entries);
  poke16(bpb, BPB_TOTSEC, fmt->total);
  bpb[BPB_MEDIA] = media;
  poke16(bpb, BPB_FATSIZE, fmt->fat_size);
  poke16(bpb, BPB_SECPERTRK, fmt->sectors_per_track);
  poke16(bpb, BPB_HEADS, fmt->heads);
}

/*
 * Build a DPB for a drive from its media descriptor.
 *
 * This is what MSX-DOS 1 expects of GETDPB, which is why the kernel also
 * passes the first byte of the FAT: between them the descriptor names one
 * of the eight standard shapes. Reading the disk's own BPB would describe
 * an odd sized image more faithfully, but it would need somewhere to put
 * the sector it read, and the memory that would take is worth more to the
 * machine than the handful of images it would help. DOS 2 reads the boot
 * sector for itself in any case.
 */
static void disk_build_dpb(uint8_t *dpb, uint8_t media)
{
  uint8_t bpb[BPB_SIZE];


  if (media < MEDIA_FIRST)
    media = MEDIA_DEFAULT;
  bpb_from_media(bpb, media);
  disk_dpb_from_bpb(dpb, bpb);
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
  uint16_t total = media_total(regs->c);
  uint8_t err = DISK_ERR_OTHER;


  if (drive >= FN_MAX_DEV)
    goto fail;

  while (count) {
    if (total && sector >= total) {
      err = DISK_ERR_NOT_FOUND;
      goto fail;
    }

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
  uint32_t mounted;
  /* The reply covers every slot at once, so it is staged here rather than
     kept in the work area: the stack gives the space back afterwards. */
  uint8_t reply[MOUNT_TIME_REPLY_SIZE];


  if (drive >= FN_MAX_DEV) {
    regs->a = DISK_ERR_OTHER;
    regs->f |= REG_CARRY;
    return;
  }

  if (!disk_call(FUJI_DEVICEID_FUJINET, FUJICMD_STATUS, FUJI_FIELD_A1,
                 STATUS_MOUNT_TIME, 0,
                 reply, MOUNT_TIME_REPLY_SIZE, 1)) {
    regs->a = DISK_ERR_NOT_READY;
    regs->f |= REG_CARRY;
    return;
  }

  /* One 64 bit mount time per slot; the low half is all that can move in
     any timeframe worth worrying about. */
  mounted = peek32(reply, (uint8_t) (drive * MOUNT_TIME_ENTRY));

  if (!mounted)
    regs->b = DISK_UNKNOWN;     /* nothing mounted in this slot */
  else if (mounted != work->mount_time[drive]) {
    work->mount_time[drive] = mounted;
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
  regs->hl = (uint16_t) choice_text;
  return;
}

/*
 * DSKFMT (401Ch): put a fresh filesystem on the mounted image.
 *
 * In:  A = choice from CHOICE, numbered from 1, D = drive
 * Out: Cy = 0 on success, otherwise Cy = 1 and A = error code
 *
 * There is no physical medium to lay tracks on, so formatting means
 * writing a boot sector, clearing both FATs and emptying the root
 * directory. Everything past the root directory is free space by
 * definition and is left alone.
 *
 * The kernel also passes a work area, which the documentation variously
 * describes as HL/DE or HL/BC. It is not needed either way, since every
 * sector this writes is a ready made image in ROM, so it is ignored.
 *
 * Note that this reformats the image in place and cannot resize it: asking
 * for 360K on a 720K image leaves a 360K filesystem in a 720K file.
 */
void __FASTCALL__ disk_fmt(MsxRegs *regs)
{
  uint8_t choice = regs->a;
  uint8_t drive = (uint8_t) (regs->de >> 8);
  uint8_t media;
  const uint8_t *boot, *fat_head;
  const MsxFormat *fmt;
  uint16_t sector, first_data;


  if (drive >= FN_MAX_DEV || !choice || choice > sizeof(format_choices)) {
    regs->a = DISK_ERR_OTHER;
    regs->f |= REG_CARRY;
    return;
  }

  media = format_choices[choice - 1];
  fmt = &msx_formats[media - MEDIA_FIRST];

  if (choice == 1) {
    boot = boot_360k;
    fat_head = fat_head_360k;
  }
  else {
    boot = boot_720k;
    fat_head = fat_head_720k;
  }

  if (!disk_write_sector(drive, 0, boot))
    goto fail;

  /* Clear both FATs and the root directory */
  first_data = 1 + 2 * (uint16_t) fmt->fat_size
    + (fmt->dir_entries + DIR_PER_SECTOR - 1) / DIR_PER_SECTOR;
  for (sector = 1; sector < first_data; sector++)
    if (!disk_write_sector(drive, sector, empty_sector))
      goto fail;

  /* Both FAT copies open with the reserved cluster entries */
  if (!disk_write_sector(drive, 1, fat_head))
    goto fail;
  if (!disk_write_sector(drive, 1 + fmt->fat_size, fat_head))
    goto fail;

  regs->f &= ~REG_CARRY;
  return;

 fail:
  /* A slot mounted read only refuses every one of these writes */
  regs->a = DISK_ERR_WRITE_PROTECT;
  regs->f |= REG_CARRY;
  return;
}
