#pragma once

#include <stdint.h>

#define MEM_ID_SIZE 8
#define MEM_PW_SIZE 8

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
    char ID[MEM_ID_SIZE];
    char PW[MEM_PW_SIZE];
} MemData;
