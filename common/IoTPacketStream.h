#pragma once

#include "IoTProtocol.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Builder: header first, then Push* values (AllocPacket + operator<< style). ---- */
typedef struct _IoTPacketWriter
{
    uint8_t *buffer;
    size_t capacity;
    size_t length;    /* header + payload written so far */
    uint8_t overflow; /* set once a Push* did not fit; IoTPacketEnd() then fails */
} IoTPacketWriter;

/* Reserves the HEADER_SIZE header; IoTPacketEnd() fills length and crc16. */
void IoTPacketBegin(IoTPacketWriter *writer, uint8_t *buffer, size_t capacity, uint16_t cmd);
void IoTPacketPushUint8(IoTPacketWriter *writer, uint8_t value);
void IoTPacketPushUint16(IoTPacketWriter *writer, uint16_t value);
void IoTPacketPushBytes(IoTPacketWriter *writer, const void *data, size_t length);
/* Fills length and crc16. Returns the frame length, or 0 on overflow or a payload over MAX_PAYLOAD_SIZE. */
size_t IoTPacketEnd(IoTPacketWriter *writer);

/* ---- Reader: Pop* values from a received payload (operator>> style). ---- */
typedef struct _IoTPacketReader
{
    const uint8_t *data;
    size_t length;
    size_t offset;
    uint8_t underflow; /* set once a Pop* ran past the end; popped values are 0 */
} IoTPacketReader;

void IoTPacketOpen(IoTPacketReader *reader, const uint8_t *payload, size_t length);
uint8_t IoTPacketPopUint8(IoTPacketReader *reader);
uint16_t IoTPacketPopUint16(IoTPacketReader *reader);
void IoTPacketPopBytes(IoTPacketReader *reader, void *out, size_t length);
/* Returns 0 when every byte was read without underflow, otherwise -1. */
int IoTPacketCheckRead(const IoTPacketReader *reader);

/* ---- Header ---- */
void DecodePacketHeader(const uint8_t *buffer, HeaderData *header);
/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Pass 0xFFFF as crc to start, the result to continue. */
uint16_t UpdatePacketCrc(uint16_t crc, const uint8_t *data, size_t length);
/* Returns 0 when header->crc matches header bytes 0..5 (headerData) + payload, otherwise -1. */
int CheckPacketCrc(const uint8_t *headerData, const HeaderData *header, const uint8_t *payload);

#ifdef __cplusplus
}
#endif
