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
#define CON_DATA_SIZE 2
#define MEM_DATA_SIZE (MEM_ID_SIZE + MEM_PW_SIZE)
#define BLUETOOTH_REGISTER_DATA_SIZE (BLUETOOTH_MAC_SIZE + BLUETOOTH_PIN_SIZE)
#define BLUETOOTH_CONNECT_DATA_SIZE (MEM_ID_SIZE + MEM_PW_SIZE + BLUETOOTH_MAC_SIZE)
#define RESULT_DATA_SIZE 1
/* NFY_CHAT text limit, kept small for 8-bit devices. */
#define MAX_CHAT_SIZE 255

typedef struct _DhtData
{
    uint16_t temp;
    uint16_t humi;
} DhtData;

typedef struct _FanData
{
    uint16_t fanSpeed;
} FanData;

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

#ifdef __cplusplus
}
#endif
