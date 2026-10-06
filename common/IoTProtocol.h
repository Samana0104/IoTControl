#pragma once

#include <stdint.h>

#define HEADER_SIZE 4
#define MAX_MESSAGE_SIZE UINT8_MAX

#define HEADER_OK_0 'o'
#define HEADER_OK_1 'k'
#define HEADER_REQUEST_0 'R'
#define HEADER_REQUEST_1 'Q'

/* RQ.dataLen carries a flag; OK.dataLen carries the payload length. */
#define RQ_FLAG_INITIAL 0
#define RQ_FLAG_RETRY 1

typedef struct _HeaderData
{
    char head0;
    char head1;
    uint8_t cmd;
    uint8_t dataLen;
} HeaderData;

typedef enum
{
    CMD_DHT11_DATA = 0,
    CMD_FAN_DATA,
    CMD_CON_DATA,
    CMD_MEM_DATA,
    CMD_CHAT_DATA,
    CMD_BLUETOOTH_REGISTER
} CmdList;
