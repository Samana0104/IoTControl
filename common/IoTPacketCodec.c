#include "IoTPacketCodec.h"
#include <string.h>

/* Shared by Raspberry5, STM32, Arduino and PC: no dynamic allocation, no stdio. */

/* ---- Make*Packet ---- */

int CheckPacketLength(uint16_t cmd, size_t length)
{
    switch(cmd)
    {
        case REQ_LOGIN: return length == MEM_DATA_SIZE ? 0 : -1;
        case REQ_DHT_ALL:
        case REQ_FAN_QUERY: return length == 0 ? 0 : -1;
        case ACK_FAN_QUERY: return length == FAN_QUERY_ACK_DATA_SIZE ? 0 : -1;
        case NFY_DHT_ROW: return length == DHT_ROW_DATA_SIZE ? 0 : -1;
        case REQ_BT_REGISTER: return length == BLUETOOTH_REGISTER_DATA_SIZE ? 0 : -1;
        case REQ_BT_CONNECT: return length == BLUETOOTH_CONNECT_DATA_SIZE ? 0 : -1;
        case REQ_FAN:
        case REQ_FAN_UPDATE: return length == FAN_DATA_SIZE ? 0 : -1;
        case REQ_DHT:
        case REQ_DHT_COLLECT: return length == 0 ? 0 : -1;
        case ACK_DHT: return length == DHT_ACK_DATA_SIZE ? 0 : -1;
        case ACK_LOGIN:
        case ACK_DHT_ALL:
        case ACK_BT_REGISTER:
        case ACK_BT_CONNECT:
        case ACK_FAN:
        case ACK_FAN_UPDATE:
        case ACK_DHT_COLLECT: return length == RESULT_DATA_SIZE ? 0 : -1;
        case NFY_CHAT: return length <= MAX_CHAT_SIZE ? 0 : -1;
        case NFY_DHT: return length == DHT_DATA_SIZE ? 0 : -1;
        case NFY_FAN: return length == FAN_DATA_SIZE ? 0 : -1;
        case NFY_CON: return length == CON_DATA_SIZE ? 0 : -1;
        default: return -1;
    }
}

size_t MakeDhtPacket(uint8_t *buffer, size_t size, const DhtData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, NFY_DHT);
    IoTPacketPushUint16(&writer, data->temp);
    IoTPacketPushUint16(&writer, data->humi);
    return IoTPacketEnd(&writer);
}

size_t MakeFanPacket(uint8_t *buffer, size_t size, const FanData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, NFY_FAN);
    IoTPacketPushUint16(&writer, data->fanSpeed);
    return IoTPacketEnd(&writer);
}

size_t MakeFanQueryPacket(uint8_t *buffer, size_t size)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_FAN_QUERY);
    return IoTPacketEnd(&writer);
}

size_t MakeFanUpdatePacket(uint8_t *buffer, size_t size, const FanData *data)
{
    IoTPacketWriter writer;

    if(data == NULL || data->fanSpeed > 100)
    {
        return 0;
    }
    IoTPacketBegin(&writer, buffer, size, REQ_FAN_UPDATE);
    IoTPacketPushUint16(&writer, data->fanSpeed);
    return IoTPacketEnd(&writer);
}

size_t MakeFanQueryAckPacket(uint8_t *buffer, size_t size, const FanQueryAckData *data)
{
    IoTPacketWriter writer;

    if(data == NULL)
    {
        return 0;
    }
    IoTPacketBegin(&writer, buffer, size, ACK_FAN_QUERY);
    IoTPacketPushUint8(&writer, data->result);
    IoTPacketPushUint16(&writer, data->fan.fanSpeed);
    return IoTPacketEnd(&writer);
}

size_t MakeFanControlPacket(uint8_t *buffer, size_t size, const FanData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_FAN);
    IoTPacketPushUint16(&writer, data->fanSpeed);
    return IoTPacketEnd(&writer);
}

size_t MakeConPacket(uint8_t *buffer, size_t size, const ConData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, NFY_CON);
    IoTPacketPushUint16(&writer, data->tempData);
    return IoTPacketEnd(&writer);
}

size_t MakeLoginPacket(uint8_t *buffer, size_t size, const MemData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_LOGIN);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    IoTPacketPushBytes(&writer, data->pw, MEM_PW_SIZE);
    return IoTPacketEnd(&writer);
}

size_t MakeChatPacket(uint8_t *buffer, size_t size, const char *message, size_t length)
{
    IoTPacketWriter writer;

    if(length > MAX_CHAT_SIZE)
    {
        return 0;
    }
    IoTPacketBegin(&writer, buffer, size, NFY_CHAT);
    IoTPacketPushBytes(&writer, message, length);
    return IoTPacketEnd(&writer);
}

size_t MakeBluetoothRegisterPacket(uint8_t *buffer, size_t size, const BluetoothRegisterData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_BT_REGISTER);
    IoTPacketPushBytes(&writer, data->mac, BLUETOOTH_MAC_SIZE);
    IoTPacketPushBytes(&writer, data->pin, BLUETOOTH_PIN_SIZE);
    return IoTPacketEnd(&writer);
}

size_t MakeBluetoothConnectPacket(uint8_t *buffer, size_t size, const BluetoothConnectData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_BT_CONNECT);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    IoTPacketPushBytes(&writer, data->pw, MEM_PW_SIZE);
    IoTPacketPushBytes(&writer, data->mac, BLUETOOTH_MAC_SIZE);
    return IoTPacketEnd(&writer);
}

size_t MakeDhtRequestPacket(uint8_t *buffer, size_t size)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_DHT);
    return IoTPacketEnd(&writer);
}

size_t MakeDhtCollectPacket(uint8_t *buffer, size_t size)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_DHT_COLLECT);
    return IoTPacketEnd(&writer);
}

size_t MakeDhtAckPacket(uint8_t *buffer, size_t size, const DhtAckData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, ACK_DHT);
    IoTPacketPushUint8(&writer, data->result);
    IoTPacketPushUint16(&writer, data->dht.temp);
    IoTPacketPushUint16(&writer, data->dht.humi);
    return IoTPacketEnd(&writer);
}

size_t MakeAckPacket(uint8_t *buffer, size_t size, uint16_t reqCmd, uint8_t result)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_TO_ACK(reqCmd));
    IoTPacketPushUint8(&writer, result);
    return IoTPacketEnd(&writer);
}

/* ---- Read* ---- */

int ReadDhtData(const uint8_t *payload, size_t length, DhtData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->temp = IoTPacketPopUint16(&reader);
    data->humi = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadFanData(const uint8_t *payload, size_t length, FanData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->fanSpeed = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadFanQueryAckData(const uint8_t *payload, size_t length, FanQueryAckData *data)
{
    IoTPacketReader reader;

    if(payload == NULL || data == NULL || length != FAN_QUERY_ACK_DATA_SIZE)
    {
        return -1;
    }
    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    data->fan.fanSpeed = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadConData(const uint8_t *payload, size_t length, ConData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->tempData = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadMemData(const uint8_t *payload, size_t length, MemData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    IoTPacketPopBytes(&reader, data->pw, MEM_PW_SIZE);
    return IoTPacketCheckRead(&reader);
}

int ReadBluetoothRegisterData(const uint8_t *payload, size_t length, BluetoothRegisterData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->mac, BLUETOOTH_MAC_SIZE);
    IoTPacketPopBytes(&reader, data->pin, BLUETOOTH_PIN_SIZE);
    return IoTPacketCheckRead(&reader);
}

int ReadBluetoothConnectData(const uint8_t *payload, size_t length, BluetoothConnectData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    IoTPacketPopBytes(&reader, data->pw, MEM_PW_SIZE);
    IoTPacketPopBytes(&reader, data->mac, BLUETOOTH_MAC_SIZE);
    return IoTPacketCheckRead(&reader);
}

int ReadDhtAckData(const uint8_t *payload, size_t length, DhtAckData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    data->dht.temp = IoTPacketPopUint16(&reader);
    data->dht.humi = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadResultData(const uint8_t *payload, size_t length, ResultData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}

static void PushFloat32(IoTPacketWriter *writer, float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, 4);
    IoTPacketPushUint16(writer, (uint16_t)(bits & 0xffffu));
    IoTPacketPushUint16(writer, (uint16_t)(bits >> 16));
}

static float PopFloat32(IoTPacketReader *reader)
{
    uint32_t bits = IoTPacketPopUint16(reader);
    float value = 0;
    bits |= (uint32_t)IoTPacketPopUint16(reader) << 16;
    memcpy(&value, &bits, 4);
    return value;
}

size_t MakeDhtAllPacket(uint8_t *buffer, size_t size)
{
    IoTPacketWriter writer;
    if(buffer == NULL)
        return 0;
    IoTPacketBegin(&writer, buffer, size, REQ_DHT_ALL);
    return IoTPacketEnd(&writer);
}

size_t MakeDhtRowPacket(uint8_t *buffer, size_t size, const DhtRowData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL || sizeof(float) != 4)
        return 0;
    IoTPacketBegin(&writer, buffer, size, NFY_DHT_ROW);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    PushFloat32(&writer, data->temp);
    PushFloat32(&writer, data->humi);
    IoTPacketPushBytes(&writer, data->updatedAt, DHT_TIMESTAMP_SIZE);
    IoTPacketPushUint8(&writer, data->memberType);
    return IoTPacketEnd(&writer);
}

int ReadDhtRowData(const uint8_t *payload, size_t length, DhtRowData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != DHT_ROW_DATA_SIZE || sizeof(float) != 4)
        return -1;
    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    data->temp = PopFloat32(&reader);
    data->humi = PopFloat32(&reader);
    IoTPacketPopBytes(&reader, data->updatedAt, DHT_TIMESTAMP_SIZE);
    data->memberType = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}
