#ifndef FUJINET_H
#define FUJINET_H

#include <stdint.h>

/* The subset of the FujiNet device IDs, command IDs and reply structures
   that the disk driver needs. The canonical lists live in fujiDeviceID.h
   and fujiCommandID.h at the top of the fujiversal tree; they are not
   included directly to keep the ROM build free of paths outside msx/. */

#define FN_MAX_DEV      8       /* FujiNet exposes eight disk device slots */

enum {
  FUJI_DEVICEID_DISK    = 0x31,
  FUJI_DEVICEID_FUJINET = 0x70,
};

enum {
  FUJICMD_READ              = 'R',
  FUJICMD_WRITE             = 'W',
  FUJICMD_STATUS            = 'S',
  FUJICMD_READ_DEVICE_SLOTS = 0xF2,
};

/* Describes the aux bytes that follow the packet header */
enum {
  FUJI_FIELD_NONE        = 0,
  FUJI_FIELD_A1          = 1,
  FUJI_FIELD_A1_A2       = 2,
  FUJI_FIELD_A1_A2_A3    = 3,
  FUJI_FIELD_A1_A2_A3_A4 = 4,
  FUJI_FIELD_B12         = 5,
  FUJI_FIELD_B12_B34     = 6,
  FUJI_FIELD_C1234       = 7,
};

enum {
  SLOT_READONLY  = 1,
  SLOT_READWRITE = 2,
};

/* Sub-command for FUJICMD_STATUS on the control device */
#define STATUS_MOUNT_TIME       0x01

/* FUJICMD_STATUS/STATUS_MOUNT_TIME replies with one 64-bit mount time per
   device slot. Only the low half is ever compared. */
#define MOUNT_TIME_ENTRY        8
#define MOUNT_TIME_REPLY_SIZE   (MOUNT_TIME_ENTRY * FN_MAX_DEV)

/* One entry per device slot in the FUJICMD_READ_DEVICE_SLOTS reply */
typedef struct {
  uint8_t host_slot;
  uint8_t mode;                 /* SLOT_READONLY or SLOT_READWRITE */
  char    file[36];
} deviceSlot_t;

#endif /* FUJINET_H */
