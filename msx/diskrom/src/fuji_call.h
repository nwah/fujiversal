#ifndef FUJI_CALL_H
#define FUJI_CALL_H

#include <stdint.h>

typedef struct {
  uint8_t device;
  uint8_t command;
  uint8_t aux_descr;
  uint8_t aux[4];
  void *buffer;
  uint16_t length;
} FujiNetParams;

/* Yes, using SIO convention internally! Legacy FTW! ;-) */
typedef enum {
    SIO_DIRECTION_NONE    = 0x00,
    SIO_DIRECTION_READ    = 0x40,
    SIO_DIRECTION_WRITE   = 0x80,
    SIO_DIRECTION_INVALID = 0xFF,
} AtariSIODirection;

typedef struct {
  uint8_t device;   /* Destination Device */
  uint8_t command;  /* Command */
  uint16_t length;  /* Total length of packet including header */
  uint8_t checksum; /* Checksum of entire packet */
  uint8_t fields;   /* Describes the fields that follow */
} fujibus_header;

typedef struct {
  fujibus_header header;
  uint8_t data[4]; /* max 4 aux bytes */
} fujibus_packet;

/* The bus call itself. The driver fills in device, command, fields and the
   aux bytes; length and checksum are worked out here. There is no
   FujiNetParams wrapper in this ROM -- the extra layer cost bytes this 16K
   image does not have. */
extern uint8_t fuji_packet_call(AtariSIODirection direction,
                                fujibus_packet *packet, void *pbuf,
                                uint16_t plen);

#endif /* FUJI_CALL_H */
