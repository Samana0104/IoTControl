#pragma once

#include "IoTProtocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_ID_SIZE 8
#define MEM_PW_SIZE 64
#define BLUETOOTH_MAC_SIZE 17
#define BLUETOOTH_PIN_SIZE 16

/* Payload sizes on the wire. Multi-byte values are little-endian, strings are
   fixed-width and not guaranteed to be null-terminated. Do not use sizeof(struct). */
#define DHT_DATA_SIZE 4
#define FAN_DATA_SIZE 2
#define FAN_QUERY_ACK_DATA_SIZE (RESULT_DATA_SIZE + FAN_DATA_SIZE)
#define CON_DATA_SIZE 2
#define MEM_DATA_SIZE (MEM_ID_SIZE + MEM_PW_SIZE)
#define BLUETOOTH_REGISTER_DATA_SIZE (BLUETOOTH_MAC_SIZE + BLUETOOTH_PIN_SIZE)
#define BLUETOOTH_CONNECT_DATA_SIZE (MEM_ID_SIZE + MEM_PW_SIZE + BLUETOOTH_MAC_SIZE)
#define RESULT_DATA_SIZE 1
/* Firmware image bytes per REQ_FW_CHUNK. */
#define FIRMWARE_CHUNK_SIZE 256
#define FIRMWARE_BEGIN_DATA_SIZE 12
#define FIRMWARE_CHUNK_OFFSET_SIZE 4
#define FIRMWARE_CHUNK_ACK_DATA_SIZE (RESULT_DATA_SIZE + 4)
#define DHT_ACK_DATA_SIZE (RESULT_DATA_SIZE + DHT_DATA_SIZE)
/* NFY_CHAT text limit, kept small for 8-bit devices. */
#define MAX_CHAT_SIZE 255

typedef struct _DhtData
{
    uint16_t temp;
    uint16_t humi;
} DhtData;

#define DHT_TIMESTAMP_SIZE 19
#define DHT_ROW_DATA_SIZE (MEM_ID_SIZE + 4 + 4 + DHT_TIMESTAMP_SIZE + 1)
#define DHT_MEMBER_UNKNOWN 0
#define DHT_MEMBER_STM32 1
#define DHT_MEMBER_ARDUINO 2
#define DHT_MEMBER_PC 3

// Float32 values are IEEE-754 little-endian. Timestamp is YYYY-MM-DD HH:MM:SS,
// or all zero bytes for SQL NULL. The server's database clock is preserved.
typedef struct _DhtRowData
{
    char id[MEM_ID_SIZE];
    float temp;
    float humi;
    char updatedAt[DHT_TIMESTAMP_SIZE];
    uint8_t memberType;
} DhtRowData;

#define SESSION_ROW_DATA_SIZE (MEM_ID_SIZE + 1 + 1)
#define SESSION_MAX_ROWS 64
#define SESSION_MEMBER_STM32 1
#define SESSION_MEMBER_ARDUINO 2
#define SESSION_LINK_TCP 0x01
#define SESSION_LINK_BT 0x02

/* One unique authenticated field device. links may contain TCP and BT together.
   Member IDs are fixed-width; internal server file descriptors are not exposed. */
typedef struct _SessionRowData
{
    char id[MEM_ID_SIZE];
    uint8_t memberType;
    uint8_t links;
} SessionRowData;
#define DHT_REFRESH_DATA_SIZE MEM_ID_SIZE
#define DHT_REFRESH_ACK_DATA_SIZE 2

typedef enum
{
    DHT_REFRESH_OK = 0,
    DHT_REFRESH_INVALID_TARGET,
    DHT_REFRESH_NOT_CONNECTED,
    DHT_REFRESH_BUSY,
    DHT_REFRESH_SEND_FAILED,
    DHT_REFRESH_REJECTED,
    DHT_REFRESH_DB_ERROR,
    DHT_REFRESH_TIMEOUT,
    DHT_REFRESH_DISCONNECTED,
    DHT_REFRESH_NOT_ALLOWED
} DhtRefreshReason;

typedef struct _DhtRefreshData
{
    char id[MEM_ID_SIZE];
} DhtRefreshData;

typedef struct _DhtRefreshAckData
{
    uint8_t result;
    uint8_t reason;
} DhtRefreshAckData;

typedef struct _FanData
{
    uint16_t fanSpeed;
} FanData;

/* ACK_FAN_QUERY: fan is valid only when result == RESULT_SUCCESS. */
typedef struct _FanQueryAckData
{
    uint8_t result;
    FanData fan;
} FanQueryAckData;

#define FAN_APPLY_DATA_SIZE (MEM_ID_SIZE + FAN_DATA_SIZE)
#define FAN_APPLY_ACK_DATA_SIZE (RESULT_DATA_SIZE + 1 + FAN_DATA_SIZE)

typedef enum
{
    FAN_APPLY_OK = 0,
    FAN_APPLY_INVALID_TARGET,
    FAN_APPLY_NOT_CONNECTED,
    FAN_APPLY_DB_ERROR,
    FAN_APPLY_DB_CHANGED,
    FAN_APPLY_BUSY,
    FAN_APPLY_SEND_FAILED,
    FAN_APPLY_REJECTED,
    FAN_APPLY_TIMEOUT,
    FAN_APPLY_DISCONNECTED,
    FAN_APPLY_NOT_ALLOWED
} FanApplyReason;

typedef struct _FanApplyData
{
    char id[MEM_ID_SIZE];
    FanData fan; /* Expected saved value; server SELECT must still match. */
} FanApplyData;

typedef struct _FanApplyAckData
{
    uint8_t result;
    uint8_t reason;
    FanData fan; /* DB percent used for this attempt, not a measured RPM. */
} FanApplyAckData;

typedef struct _ConData
{
    uint16_t tempData;
} ConData;

/* REQ_LOGIN payload */
typedef struct _MemData
{
    /* Fixed-width fields; they are not guaranteed to be null-terminated. */
    char id[MEM_ID_SIZE];
    char pw[MEM_PW_SIZE];
} MemData;

typedef struct _BluetoothRegisterData
{
    /* Fixed-width fields; they are not guaranteed to be null-terminated. */
    char mac[BLUETOOTH_MAC_SIZE];
    char pin[BLUETOOTH_PIN_SIZE];
} BluetoothRegisterData;

typedef struct _BluetoothConnectData
{
    /* Credentials are not retained by the BT client. */
    char id[MEM_ID_SIZE];
    char pw[MEM_PW_SIZE];
    char mac[BLUETOOTH_MAC_SIZE];
} BluetoothConnectData;

/* Every ACK payload */
typedef struct _ResultData
{
    uint8_t result; /* RESULT_SUCCESS or RESULT_FAIL */
} ResultData;

/* ACK_DHT payload. dht is valid only when result == RESULT_SUCCESS. */
typedef struct _DhtAckData
{
    uint8_t result;
    DhtData dht;
} DhtAckData;

/* REQ_FW_BEGIN payload */
typedef struct _FirmwareBeginData
{
    uint32_t size;    /* image bytes, 1..FIRMWARE_APP_MAX_SIZE */
    uint32_t crc32;   /* ComputeFirmwareCrc32 of the whole image */
    uint32_t version; /* FirmwareInfo.version inside the image */
} FirmwareBeginData;

/* REQ_FW_CHUNK payload: offset, then length bytes (length comes from the frame length). */
typedef struct _FirmwareChunkData
{
    uint32_t offset;
    uint16_t length;
    uint8_t data[FIRMWARE_CHUNK_SIZE];
} FirmwareChunkData;

/* ACK_FW_CHUNK payload: offset echoes the acknowledged chunk. */
typedef struct _FirmwareChunkAckData
{
    uint8_t result;
    uint32_t offset;
} FirmwareChunkAckData;

#ifdef __cplusplus
}
#endif
