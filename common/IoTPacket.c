#include "IoTPacket.h"

#include <string.h>

/* Shared by Raspberry5, STM32, Arduino and PC: no dynamic allocation, no stdio. */

static void PutUint16(uint8_t *buffer, uint16_t value);
static uint16_t GetUint16(const uint8_t *buffer);

void EncodePacketHeader(uint8_t *buffer, char head0, char head1, uint8_t cmd, uint8_t dataLen)
{
    buffer[0] = (uint8_t)head0;
    buffer[1] = (uint8_t)head1;
    buffer[2] = cmd;
    buffer[3] = dataLen;
}

void DecodePacketHeader(const uint8_t *buffer, HeaderData *header)
{
    header->head0 = (char)buffer[0];
    header->head1 = (char)buffer[1];
    header->cmd = buffer[2];
    header->dataLen = buffer[3];
}

size_t EncodeDhtData(const DhtData *data, uint8_t *buffer, size_t size)
{
    if(size < DHT_DATA_SIZE)
    {
        return 0;
    }
    PutUint16(buffer, data->temp);
    PutUint16(buffer + 2, data->humi);
    return DHT_DATA_SIZE;
}

int DecodeDhtData(const uint8_t *buffer, size_t length, DhtData *data)
{
    if(length != DHT_DATA_SIZE)
    {
        return -1;
    }
    data->temp = GetUint16(buffer);
    data->humi = GetUint16(buffer + 2);
    return 0;
}

size_t EncodeFanData(const FanData *data, uint8_t *buffer, size_t size)
{
    if(size < FAN_DATA_SIZE)
    {
        return 0;
    }
    PutUint16(buffer, data->fanSpeed);
    return FAN_DATA_SIZE;
}

int DecodeFanData(const uint8_t *buffer, size_t length, FanData *data)
{
    if(length != FAN_DATA_SIZE)
    {
        return -1;
    }
    data->fanSpeed = GetUint16(buffer);
    return 0;
}

size_t EncodeConData(const ConData *data, uint8_t *buffer, size_t size)
{
    if(size < CON_DATA_SIZE)
    {
        return 0;
    }
    PutUint16(buffer, data->tempData);
    return CON_DATA_SIZE;
}

int DecodeConData(const uint8_t *buffer, size_t length, ConData *data)
{
    if(length != CON_DATA_SIZE)
    {
        return -1;
    }
    data->tempData = GetUint16(buffer);
    return 0;
}

size_t EncodeMemData(const MemData *data, uint8_t *buffer, size_t size)
{
    if(size < MEM_DATA_SIZE)
    {
        return 0;
    }
    memcpy(buffer, data->id, MEM_ID_SIZE);
    memcpy(buffer + MEM_ID_SIZE, data->pw, MEM_PW_SIZE);
    return MEM_DATA_SIZE;
}

int DecodeMemData(const uint8_t *buffer, size_t length, MemData *data)
{
    if(length != MEM_DATA_SIZE)
    {
        return -1;
    }
    memcpy(data->id, buffer, MEM_ID_SIZE);
    memcpy(data->pw, buffer + MEM_ID_SIZE, MEM_PW_SIZE);
    return 0;
}

size_t EncodeBluetoothRegisterData(const BluetoothRegisterData *data, uint8_t *buffer, size_t size)
{
    if(size < BLUETOOTH_REGISTER_DATA_SIZE)
    {
        return 0;
    }
    memcpy(buffer, data->mac, BLUETOOTH_MAC_SIZE);
    memcpy(buffer + BLUETOOTH_MAC_SIZE, data->pin, BLUETOOTH_PIN_SIZE);
    return BLUETOOTH_REGISTER_DATA_SIZE;
}

int DecodeBluetoothRegisterData(const uint8_t *buffer, size_t length, BluetoothRegisterData *data)
{
    if(length != BLUETOOTH_REGISTER_DATA_SIZE)
    {
        return -1;
    }
    memcpy(data->mac, buffer, BLUETOOTH_MAC_SIZE);
    memcpy(data->pin, buffer + BLUETOOTH_MAC_SIZE, BLUETOOTH_PIN_SIZE);
    return 0;
}

size_t EncodeBluetoothConnectData(const BluetoothConnectData *data, uint8_t *buffer, size_t size)
{
    if(size < BLUETOOTH_CONNECT_DATA_SIZE)
    {
        return 0;
    }
    memcpy(buffer, data->id, MEM_ID_SIZE);
    memcpy(buffer + MEM_ID_SIZE, data->pw, MEM_PW_SIZE);
    memcpy(buffer + MEM_ID_SIZE + MEM_PW_SIZE, data->mac, BLUETOOTH_MAC_SIZE);
    return BLUETOOTH_CONNECT_DATA_SIZE;
}

int DecodeBluetoothConnectData(const uint8_t *buffer, size_t length, BluetoothConnectData *data)
{
    if(length != BLUETOOTH_CONNECT_DATA_SIZE)
    {
        return -1;
    }
    memcpy(data->id, buffer, MEM_ID_SIZE);
    memcpy(data->pw, buffer + MEM_ID_SIZE, MEM_PW_SIZE);
    memcpy(data->mac, buffer + MEM_ID_SIZE + MEM_PW_SIZE, BLUETOOTH_MAC_SIZE);
    return 0;
}

size_t EncodeBluetoothConnectResult(const BluetoothConnectResult *data, uint8_t *buffer, size_t size)
{
    if(size < BLUETOOTH_CONNECT_RESULT_SIZE)
    {
        return 0;
    }
    buffer[0] = data->connected;
    return BLUETOOTH_CONNECT_RESULT_SIZE;
}

int DecodeBluetoothConnectResult(const uint8_t *buffer, size_t length, BluetoothConnectResult *data)
{
    if(length != BLUETOOTH_CONNECT_RESULT_SIZE)
    {
        return -1;
    }
    data->connected = buffer[0];
    return 0;
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
