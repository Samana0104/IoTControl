#pragma once

#include "IoTProtocol.h"

#include <stddef.h>
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
#define BLUETOOTH_CONNECT_RESULT_SIZE 1

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
    /* Credentials are sent only over TLS and are not retained by the BT client. */
    char id[MEM_ID_SIZE];
    char pw[MEM_PW_SIZE];
    char mac[BLUETOOTH_MAC_SIZE];
} BluetoothConnectData;

typedef struct _BluetoothConnectResult
{
    uint8_t connected; /* 1: connected, 0: authentication/binding/connection failed. */
} BluetoothConnectResult;

/* Header: buffer holds HEADER_SIZE bytes. */
void EncodePacketHeader(uint8_t *buffer, char head0, char head1, uint8_t cmd, uint8_t dataLen);
void DecodePacketHeader(const uint8_t *buffer, HeaderData *header);

/* Encode*: writes the payload into buffer and returns its size, or 0 if size is too small.
   Decode*: returns 0 when length is exactly the payload size, otherwise -1. */
size_t EncodeDhtData(const DhtData *data, uint8_t *buffer, size_t size);
int DecodeDhtData(const uint8_t *buffer, size_t length, DhtData *data);
size_t EncodeFanData(const FanData *data, uint8_t *buffer, size_t size);
int DecodeFanData(const uint8_t *buffer, size_t length, FanData *data);
size_t EncodeConData(const ConData *data, uint8_t *buffer, size_t size);
int DecodeConData(const uint8_t *buffer, size_t length, ConData *data);
size_t EncodeMemData(const MemData *data, uint8_t *buffer, size_t size);
int DecodeMemData(const uint8_t *buffer, size_t length, MemData *data);
size_t EncodeBluetoothRegisterData(const BluetoothRegisterData *data, uint8_t *buffer, size_t size);
int DecodeBluetoothRegisterData(const uint8_t *buffer, size_t length, BluetoothRegisterData *data);
size_t EncodeBluetoothConnectData(const BluetoothConnectData *data, uint8_t *buffer, size_t size);
int DecodeBluetoothConnectData(const uint8_t *buffer, size_t length, BluetoothConnectData *data);
size_t EncodeBluetoothConnectResult(const BluetoothConnectResult *data, uint8_t *buffer, size_t size);
int DecodeBluetoothConnectResult(const uint8_t *buffer, size_t length, BluetoothConnectResult *data);

#ifdef __cplusplus
}
#endif
