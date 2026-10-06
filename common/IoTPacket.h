#pragma once

#include <stdint.h>

#define MEM_ID_SIZE 8
#define MEM_PW_SIZE 64
#define BLUETOOTH_MAC_SIZE 17
#define BLUETOOTH_PIN_SIZE 16

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
