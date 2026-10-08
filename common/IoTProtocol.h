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
#define MSG_DHT_COLLECT 0x0009
#define MSG_FAN_QUERY 0x000A
#define MSG_FW_BEGIN 0x000B
#define MSG_FW_CHUNK 0x000C
#define MSG_FW_END 0x000D
#define MSG_FAN_UPDATE 0x000E
#define MSG_SESSION_ALL 0x000F
#define MSG_FAN_APPLY 0x0010
#define MSG_DHT_REFRESH 0x0011
#define MSG_BT_CONNECT_ALL 0x0012
#define MSG_BT_SCAN 0x0013

/* Client -> server, TCP */
#define REQ_LOGIN MSG_LOGIN                 /* MemData */
#define ACK_LOGIN REQ_TO_ACK(REQ_LOGIN)     /* ResultData */
#define REQ_BT_REGISTER MSG_BT_REGISTER     /* BluetoothRegisterData, after login */
#define ACK_BT_REGISTER REQ_TO_ACK(REQ_BT_REGISTER)
#define REQ_BT_CONNECT MSG_BT_CONNECT       /* BluetoothConnectData, no login needed */
#define ACK_BT_CONNECT REQ_TO_ACK(REQ_BT_CONNECT)
#define REQ_DHT_COLLECT MSG_DHT_COLLECT     /* no payload, after login: server sends REQ_DHT to every device */
#define ACK_DHT_COLLECT REQ_TO_ACK(REQ_DHT_COLLECT) /* ResultData, SUCCESS if sent to at least one device */

/* Authenticated PC -> server: read fan singleton_id=1, no device control. */
#define REQ_FAN_QUERY MSG_FAN_QUERY /* no payload */
#define ACK_FAN_QUERY REQ_TO_ACK(REQ_FAN_QUERY) /* FanQueryAckData */

/* Authenticated PC -> server: UPDATE fan singleton_id=1 only, no device control. */
#define REQ_FAN_UPDATE MSG_FAN_UPDATE /* FanData, 0..100 percent */
#define ACK_FAN_UPDATE REQ_TO_ACK(REQ_FAN_UPDATE) /* ResultData, DB value verified after UPDATE */

/* Server -> device control (BT) */
#define REQ_FAN MSG_FAN                     /* FanData, fanSpeed = 0..100 percent */
#define ACK_FAN REQ_TO_ACK(REQ_FAN)         /* ResultData */
#define REQ_DHT MSG_DHT                     /* no payload, read DHT now */
#define ACK_DHT REQ_TO_ACK(REQ_DHT)         /* DhtAckData, server stores it like NFY_DHT */

/* Server -> STM32 firmware update (BT), see IoTFirmware.h.
   BEGIN, then CHUNK from offset 0 in order, then END; the device reboots after ACK_FW_END. */
#define REQ_FW_BEGIN MSG_FW_BEGIN           /* FirmwareBeginData: device erases its staging area */
#define ACK_FW_BEGIN REQ_TO_ACK(REQ_FW_BEGIN) /* ResultData */
#define REQ_FW_CHUNK MSG_FW_CHUNK           /* FirmwareChunkData: offset + 1..FIRMWARE_CHUNK_SIZE bytes */
#define ACK_FW_CHUNK REQ_TO_ACK(REQ_FW_CHUNK) /* FirmwareChunkAckData */
#define REQ_FW_END MSG_FW_END               /* no payload: device checks CRC32, marks the image ready */
#define ACK_FW_END REQ_TO_ACK(REQ_FW_END)   /* ResultData */

/* Device -> server reports */
#define NFY_CHAT MAKE_NOTIFY(MSG_CHAT)      /* text, 0..MAX_CHAT_SIZE bytes */
#define NFY_DHT MAKE_NOTIFY(MSG_DHT)        /* DhtData */
#define NFY_FAN MAKE_NOTIFY(MSG_FAN)        /* FanData */
#define NFY_CON MAKE_NOTIFY(MSG_CON)        /* ConData */

/* Authenticated TCP client -> server: all dht rows, then completion ACK. */
#define REQ_DHT_ALL MSG_DHT_ALL             /* no payload */
#define NFY_DHT_ROW MAKE_NOTIFY(MSG_DHT_ALL) /* DhtRowData */
#define ACK_DHT_ALL REQ_TO_ACK(REQ_DHT_ALL) /* ResultData */

/* Authenticated PC -> server: unique connected STM32/Arduino IDs from RSession.
   Zero or more NFY_SESSION_ROW frames, then ACK_SESSION_ALL. */
#define REQ_SESSION_ALL MSG_SESSION_ALL /* no payload */
#define NFY_SESSION_ROW MAKE_NOTIFY(MSG_SESSION_ALL) /* SessionRowData */
#define ACK_SESSION_ALL REQ_TO_ACK(REQ_SESSION_ALL) /* ResultData */

/* Authenticated PC -> server: apply the verified DB speed to one STM32 BT ID.
   Existing device REQ_FAN/ACK_FAN remain unchanged. */
#define REQ_FAN_APPLY MSG_FAN_APPLY /* FanApplyData: ID + expected DB percent */
#define ACK_FAN_APPLY REQ_TO_ACK(REQ_FAN_APPLY) /* FanApplyAckData, sent after device ACK or failure */

/* PC -> server: one field ID, completion only after ACK_DHT and DB INSERT. */
#define REQ_DHT_REFRESH MSG_DHT_REFRESH /* DhtRefreshData */
#define ACK_DHT_REFRESH REQ_TO_ACK(REQ_DHT_REFRESH) /* DhtRefreshAckData */

#define REQ_BT_CONNECT_ALL MSG_BT_CONNECT_ALL
#define NFY_BT_CONNECT_ROW MAKE_NOTIFY(MSG_BT_CONNECT_ALL)
#define ACK_BT_CONNECT_ALL REQ_TO_ACK(REQ_BT_CONNECT_ALL)
#define REQ_BT_SCAN MSG_BT_SCAN
#define NFY_BT_SCAN_ROW MAKE_NOTIFY(MSG_BT_SCAN)
#define ACK_BT_SCAN REQ_TO_ACK(REQ_BT_SCAN)
