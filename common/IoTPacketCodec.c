#include "IoTPacketCodec.h"

/* Shared by Raspberry5, STM32, Arduino and PC: no dynamic allocation, no stdio. */

/* ---- Make*Packet ---- */

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

int ReadResultData(const uint8_t *payload, size_t length, ResultData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}
