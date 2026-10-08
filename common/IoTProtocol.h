#pragma once

#include <stdint.h>

/* ============================================================================
   Frame = 8-byte header + payload. Multi-byte values are little-endian.

   offset 0  uint16  cmd       bit15 ACK | bit14 NFY | bit13..0 ID
   offset 2  uint16  length    payload length
   offset 4  uint16  reserved  senders write 0, receivers ignore it
   offset 6  uint16  crc16     CRC-16/CCITT-FALSE over bytes 0..5 + payload
   offset 8  payload           layout is defined by cmd (see IoTPacket.h)

   A frame is sent at once. One REQ is outstanding per link at a time, so an ACK
   is matched by cmd alone; a requester that times out closes the link.
   ============================================================================ */
#define HEADER_SIZE 8

/* Largest payload a receiver accepts. Small devices may define a lower value. */
#ifndef MAX_PAYLOAD_SIZE
#define MAX_PAYLOAD_SIZE 512
#endif
/* Any frame fits in a buffer of this size. */
#define PACKET_FRAME_SIZE (HEADER_SIZE + MAX_PAYLOAD_SIZE)

typedef struct _HeaderData
{
    uint16_t cmd;
    uint16_t length;
    uint16_t reserved;
    uint16_t crc;
} HeaderData;

/* cmd: REQ = ID, ACK = ID | 0x8000, NFY = ID | 0x4000, ID = 0x0001 ~ 0x3FFF */
#define CMD_ACK_BIT 0x8000
#define CMD_NOTIFY_BIT 0x4000
#define CMD_ID_MASK 0x3FFF

#define REQ_TO_ACK(id) ((uint16_t)((id) | CMD_ACK_BIT))
#define ACK_TO_REQ(cmd) ((uint16_t)((cmd) & CMD_ID_MASK))
#define MAKE_NOTIFY(id) ((uint16_t)((id) | CMD_NOTIFY_BIT))
#define IS_ACK(cmd) (((cmd) & CMD_ACK_BIT) != 0)
#define IS_NOTIFY(cmd) (((cmd) & CMD_NOTIFY_BIT) != 0)

/* Every ACK payload starts with one of these. */
#define RESULT_SUCCESS 0x00
#define RESULT_FAIL 0x01

/* Message IDs */
#define MSG_LOGIN 0x0001
#define MSG_CHAT 0x0002
#define MSG_DHT 0x0003
#define MSG_FAN 0x0004
#define MSG_CON 0x0005
#define MSG_BT_REGISTER 0x0006
#define MSG_BT_CONNECT 0x0007
#define MSG_DHT_ALL 0x0008

/* Client -> server, TCP */
#define REQ_LOGIN MSG_LOGIN                 /* MemData */
#define ACK_LOGIN REQ_TO_ACK(REQ_LOGIN)     /* ResultData */
#define REQ_BT_REGISTER MSG_BT_REGISTER     /* BluetoothRegisterData, after login */
#define ACK_BT_REGISTER REQ_TO_ACK(REQ_BT_REGISTER)
#define REQ_BT_CONNECT MSG_BT_CONNECT       /* BluetoothConnectData, no login needed */
#define ACK_BT_CONNECT REQ_TO_ACK(REQ_BT_CONNECT)

/* Server -> device control (BT) */
#define REQ_FAN MSG_FAN                     /* FanData, fanSpeed = 0..100 percent */
#define ACK_FAN REQ_TO_ACK(REQ_FAN)         /* ResultData */

/* Device -> server reports */
#define NFY_CHAT MAKE_NOTIFY(MSG_CHAT)      /* text, 0..MAX_CHAT_SIZE bytes */
#define NFY_DHT MAKE_NOTIFY(MSG_DHT)        /* DhtData */
#define NFY_FAN MAKE_NOTIFY(MSG_FAN)        /* FanData */
#define NFY_CON MAKE_NOTIFY(MSG_CON)        /* ConData */

/* Authenticated TCP client -> server: all dht rows, then completion ACK. */
#define REQ_DHT_ALL MSG_DHT_ALL             /* no payload */
#define NFY_DHT_ROW MAKE_NOTIFY(MSG_DHT_ALL) /* DhtRowData */
#define ACK_DHT_ALL REQ_TO_ACK(REQ_DHT_ALL) /* ResultData */
