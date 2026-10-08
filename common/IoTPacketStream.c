#include "IoTPacketStream.h"

#include <string.h>

/* Shared by Raspberry5, STM32, Arduino and PC: no dynamic allocation, no stdio.
   Multi-byte values are little-endian on the wire. */

/* ---- Builder ---- */

#define CRC_COVERED_HEADER_SIZE 6 /* cmd, length, reserved */

static void PutUint16(uint8_t *buffer, uint16_t value);
static uint16_t GetUint16(const uint8_t *buffer);

void IoTPacketBegin(IoTPacketWriter *writer, uint8_t *buffer, size_t capacity, uint16_t cmd)
{
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0;
    writer->overflow = 0;
    IoTPacketPushUint16(writer, cmd);
    IoTPacketPushUint16(writer, 0); /* length, filled by IoTPacketEnd() */
    IoTPacketPushUint16(writer, 0); /* reserved */
    IoTPacketPushUint16(writer, 0); /* crc16, filled by IoTPacketEnd() */
}

void IoTPacketPushUint8(IoTPacketWriter *writer, uint8_t value)
{
    IoTPacketPushBytes(writer, &value, 1);
}

void IoTPacketPushUint16(IoTPacketWriter *writer, uint16_t value)
{
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)(value >> 8);
    IoTPacketPushBytes(writer, bytes, sizeof(bytes));
}

void IoTPacketPushUint32(IoTPacketWriter *writer, uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)((value >> 8) & 0xFF);
    bytes[2] = (uint8_t)((value >> 16) & 0xFF);
    bytes[3] = (uint8_t)(value >> 24);
    IoTPacketPushBytes(writer, bytes, sizeof(bytes));
}

void IoTPacketPushBytes(IoTPacketWriter *writer, const void *data, size_t length)
{
    if(writer->overflow || length > writer->capacity - writer->length)
    {
        writer->overflow = 1;
        return;
    }
    memcpy(writer->buffer + writer->length, data, length);
    writer->length += length;
}

size_t IoTPacketEnd(IoTPacketWriter *writer)
{
    size_t payloadLength;
    uint16_t crc;

    if(writer->overflow || writer->length < HEADER_SIZE)
    {
        return 0;
    }
    payloadLength = writer->length - HEADER_SIZE;
    if(payloadLength > MAX_PAYLOAD_SIZE)
    {
        return 0;
    }
    PutUint16(writer->buffer + 2, (uint16_t)payloadLength);
    crc = UpdatePacketCrc(0xFFFF, writer->buffer, CRC_COVERED_HEADER_SIZE);
    crc = UpdatePacketCrc(crc, writer->buffer + HEADER_SIZE, payloadLength);
    PutUint16(writer->buffer + 6, crc);
    return writer->length;
}

/* ---- Reader ---- */

void IoTPacketOpen(IoTPacketReader *reader, const uint8_t *payload, size_t length)
{
    reader->data = payload;
    reader->length = length;
    reader->offset = 0;
    reader->underflow = 0;
}

uint8_t IoTPacketPopUint8(IoTPacketReader *reader)
{
    uint8_t value = 0;

    IoTPacketPopBytes(reader, &value, 1);
    return value;
}

uint16_t IoTPacketPopUint16(IoTPacketReader *reader)
{
    uint8_t bytes[2] = {0, 0};

    IoTPacketPopBytes(reader, bytes, sizeof(bytes));
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

uint32_t IoTPacketPopUint32(IoTPacketReader *reader)
{
    uint8_t bytes[4] = {0, 0, 0, 0};

    IoTPacketPopBytes(reader, bytes, sizeof(bytes));
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

void IoTPacketPopBytes(IoTPacketReader *reader, void *out, size_t length)
{
    if(reader->underflow || length > reader->length - reader->offset)
    {
        reader->underflow = 1;
        memset(out, 0, length);
        return;
    }
    memcpy(out, reader->data + reader->offset, length);
    reader->offset += length;
}

int IoTPacketCheckRead(const IoTPacketReader *reader)
{
    return !reader->underflow && reader->offset == reader->length ? 0 : -1;
}

/* ---- Header ---- */

void DecodePacketHeader(const uint8_t *buffer, HeaderData *header)
{
    header->cmd = GetUint16(buffer);
    header->length = GetUint16(buffer + 2);
    header->reserved = GetUint16(buffer + 4);
    header->crc = GetUint16(buffer + 6);
}

uint16_t UpdatePacketCrc(uint16_t crc, const uint8_t *data, size_t length)
{
    for(size_t index = 0; index < length; ++index)
    {
        crc ^= (uint16_t)(data[index] << 8);
        for(uint8_t bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

int CheckPacketCrc(const uint8_t *headerData, const HeaderData *header, const uint8_t *payload)
{
    uint16_t crc = UpdatePacketCrc(0xFFFF, headerData, CRC_COVERED_HEADER_SIZE);

    crc = UpdatePacketCrc(crc, payload, header->length);
    return crc == header->crc ? 0 : -1;
}

static void PutUint16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)(value >> 8);
}

static uint16_t GetUint16(const uint8_t *buffer)
{
    return (uint16_t)(buffer[0] | (buffer[1] << 8));
}
