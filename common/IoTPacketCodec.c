#include "IoTPacketCodec.h"
#include <string.h>

/* Shared by Raspberry5, STM32, Arduino and PC: no dynamic allocation, no stdio. */

/* ---- Make*Packet ---- */

int CheckPacketLength(uint16_t cmd, size_t length)
{
    switch(cmd)
    {
        case REQ_BT_CONNECT_ALL:
        case REQ_BT_SCAN: return length == 0 ? 0 : -1;
        case NFY_BT_CONNECT_ROW: return length == BT_CONNECT_ROW_SIZE ? 0 : -1;
        case NFY_BT_SCAN_ROW: return length == BT_SCAN_ROW_SIZE ? 0 : -1;
        case ACK_BT_CONNECT_ALL:
        case ACK_BT_SCAN: return length == BT_OPERATION_ACK_SIZE ? 0 : -1;
        case REQ_FAN_APPLY: return length == FAN_APPLY_DATA_SIZE ? 0 : -1;
        case ACK_FAN_APPLY: return length == FAN_APPLY_ACK_DATA_SIZE ? 0 : -1;
        case REQ_DHT_REFRESH: return length == DHT_REFRESH_DATA_SIZE ? 0 : -1;
        case ACK_DHT_REFRESH: return length == DHT_REFRESH_ACK_DATA_SIZE ? 0 : -1;
        case REQ_LOGIN: return length == MEM_DATA_SIZE ? 0 : -1;
        case REQ_SESSION_ALL:
        case REQ_DHT_ALL:
        case REQ_FAN_QUERY: return length == 0 ? 0 : -1;
        case ACK_FAN_QUERY: return length == FAN_QUERY_ACK_DATA_SIZE ? 0 : -1;
        case NFY_SESSION_ROW: return length == SESSION_ROW_DATA_SIZE ? 0 : -1;
        case NFY_DHT_ROW: return length == DHT_ROW_DATA_SIZE ? 0 : -1;
        case REQ_BT_REGISTER: return length == BLUETOOTH_REGISTER_DATA_SIZE ? 0 : -1;
        case REQ_BT_CONNECT: return length == BLUETOOTH_CONNECT_DATA_SIZE ? 0 : -1;
        case REQ_FW_BEGIN: return length == FIRMWARE_BEGIN_DATA_SIZE ? 0 : -1;
        case REQ_FW_CHUNK: return length > FIRMWARE_CHUNK_OFFSET_SIZE && length <= FIRMWARE_CHUNK_OFFSET_SIZE + FIRMWARE_CHUNK_SIZE ? 0 : -1;
        case REQ_FW_END: return length == 0 ? 0 : -1;
        case ACK_FW_CHUNK: return length == FIRMWARE_CHUNK_ACK_DATA_SIZE ? 0 : -1;
        case REQ_FAN:
        case REQ_FAN_UPDATE: return length == FAN_DATA_SIZE ? 0 : -1;
        case REQ_DHT:
        case REQ_DHT_COLLECT: return length == 0 ? 0 : -1;
        case ACK_DHT: return length == DHT_ACK_DATA_SIZE ? 0 : -1;
        case ACK_LOGIN:
        case ACK_SESSION_ALL:
        case ACK_DHT_ALL:
        case ACK_BT_REGISTER:
        case ACK_BT_CONNECT:
        case ACK_FAN:
        case ACK_FW_BEGIN:
        case ACK_FW_END:
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

size_t MakeFirmwareBeginPacket(uint8_t *buffer, size_t size, const FirmwareBeginData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_FW_BEGIN);
    IoTPacketPushUint32(&writer, data->size);
    IoTPacketPushUint32(&writer, data->crc32);
    IoTPacketPushUint32(&writer, data->version);
    return IoTPacketEnd(&writer);
}

size_t MakeFirmwareChunkPacket(uint8_t *buffer, size_t size, const FirmwareChunkData *data)
{
    IoTPacketWriter writer;

    if(data->length == 0 || data->length > FIRMWARE_CHUNK_SIZE)
    {
        return 0;
    }
    IoTPacketBegin(&writer, buffer, size, REQ_FW_CHUNK);
    IoTPacketPushUint32(&writer, data->offset);
    IoTPacketPushBytes(&writer, data->data, data->length);
    return IoTPacketEnd(&writer);
}

size_t MakeFirmwareEndPacket(uint8_t *buffer, size_t size)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, REQ_FW_END);
    return IoTPacketEnd(&writer);
}

size_t MakeFirmwareChunkAckPacket(uint8_t *buffer, size_t size, const FirmwareChunkAckData *data)
{
    IoTPacketWriter writer;

    IoTPacketBegin(&writer, buffer, size, ACK_FW_CHUNK);
    IoTPacketPushUint8(&writer, data->result);
    IoTPacketPushUint32(&writer, data->offset);
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

int ReadFirmwareBeginData(const uint8_t *payload, size_t length, FirmwareBeginData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->size = IoTPacketPopUint32(&reader);
    data->crc32 = IoTPacketPopUint32(&reader);
    data->version = IoTPacketPopUint32(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadFirmwareChunkData(const uint8_t *payload, size_t length, FirmwareChunkData *data)
{
    IoTPacketReader reader;

    if(length <= FIRMWARE_CHUNK_OFFSET_SIZE || length > FIRMWARE_CHUNK_OFFSET_SIZE + FIRMWARE_CHUNK_SIZE)
    {
        return -1;
    }
    data->length = (uint16_t)(length - FIRMWARE_CHUNK_OFFSET_SIZE);
    IoTPacketOpen(&reader, payload, length);
    data->offset = IoTPacketPopUint32(&reader);
    IoTPacketPopBytes(&reader, data->data, data->length);
    return IoTPacketCheckRead(&reader);
}

int ReadFirmwareChunkAckData(const uint8_t *payload, size_t length, FirmwareChunkAckData *data)
{
    IoTPacketReader reader;

    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    data->offset = IoTPacketPopUint32(&reader);
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

size_t MakeSessionAllPacket(uint8_t *buffer, size_t size)
{
    IoTPacketWriter writer;
    IoTPacketBegin(&writer, buffer, size, REQ_SESSION_ALL);
    return IoTPacketEnd(&writer);
}

size_t MakeSessionRowPacket(uint8_t *buffer, size_t size, const SessionRowData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL)
        return 0;
    IoTPacketBegin(&writer, buffer, size, NFY_SESSION_ROW);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    IoTPacketPushUint8(&writer, data->memberType);
    IoTPacketPushUint8(&writer, data->links);
    return IoTPacketEnd(&writer);
}

int ReadSessionRowData(const uint8_t *payload, size_t length, SessionRowData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != SESSION_ROW_DATA_SIZE)
        return -1;
    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    data->memberType = IoTPacketPopUint8(&reader);
    data->links = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}

size_t MakeFanApplyPacket(uint8_t *buffer, size_t size, const FanApplyData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL)
        return 0;
    IoTPacketBegin(&writer, buffer, size, REQ_FAN_APPLY);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    IoTPacketPushUint16(&writer, data->fan.fanSpeed);
    return IoTPacketEnd(&writer);
}

size_t MakeFanApplyAckPacket(uint8_t *buffer, size_t size, const FanApplyAckData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL)
        return 0;
    IoTPacketBegin(&writer, buffer, size, ACK_FAN_APPLY);
    IoTPacketPushUint8(&writer, data->result);
    IoTPacketPushUint8(&writer, data->reason);
    IoTPacketPushUint16(&writer, data->fan.fanSpeed);
    return IoTPacketEnd(&writer);
}

int ReadFanApplyData(const uint8_t *payload, size_t length, FanApplyData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != FAN_APPLY_DATA_SIZE)
        return -1;
    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    data->fan.fanSpeed = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

int ReadFanApplyAckData(const uint8_t *payload, size_t length, FanApplyAckData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != FAN_APPLY_ACK_DATA_SIZE)
        return -1;
    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    data->reason = IoTPacketPopUint8(&reader);
    data->fan.fanSpeed = IoTPacketPopUint16(&reader);
    return IoTPacketCheckRead(&reader);
}

size_t MakeDhtRefreshPacket(uint8_t *buffer, size_t size, const DhtRefreshData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL) return 0;
    IoTPacketBegin(&writer, buffer, size, REQ_DHT_REFRESH);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    return IoTPacketEnd(&writer);
}
size_t MakeDhtRefreshAckPacket(uint8_t *buffer, size_t size, const DhtRefreshAckData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL) return 0;
    IoTPacketBegin(&writer, buffer, size, ACK_DHT_REFRESH);
    IoTPacketPushUint8(&writer, data->result);
    IoTPacketPushUint8(&writer, data->reason);
    return IoTPacketEnd(&writer);
}
int ReadDhtRefreshData(const uint8_t *payload, size_t length, DhtRefreshData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != DHT_REFRESH_DATA_SIZE) return -1;
    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    return IoTPacketCheckRead(&reader);
}
int ReadDhtRefreshAckData(const uint8_t *payload, size_t length, DhtRefreshAckData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != DHT_REFRESH_ACK_DATA_SIZE) return -1;
    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    data->reason = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}

size_t MakeBtConnectRowPacket(uint8_t *buffer, size_t size, const BtConnectRowData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL) return 0;
    IoTPacketBegin(&writer, buffer, size, NFY_BT_CONNECT_ROW);
    IoTPacketPushBytes(&writer, data->id, MEM_ID_SIZE);
    IoTPacketPushUint8(&writer, data->status);
    return IoTPacketEnd(&writer);
}
int ReadBtConnectRowData(const uint8_t *payload, size_t length, BtConnectRowData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != BT_CONNECT_ROW_SIZE) return -1;
    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->id, MEM_ID_SIZE);
    data->status = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}

size_t MakeBtScanRowPacket(uint8_t *buffer, size_t size, const BtScanRowData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL) return 0;
    IoTPacketBegin(&writer, buffer, size, NFY_BT_SCAN_ROW);
    IoTPacketPushBytes(&writer, data->mac, BLUETOOTH_MAC_SIZE);
    IoTPacketPushBytes(&writer, data->name, BT_SCAN_NAME_SIZE);
    IoTPacketPushUint16(&writer, data->rssi);
    IoTPacketPushUint8(&writer, data->paired);
    return IoTPacketEnd(&writer);
}
int ReadBtScanRowData(const uint8_t *payload, size_t length, BtScanRowData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != BT_SCAN_ROW_SIZE) return -1;
    IoTPacketOpen(&reader, payload, length);
    IoTPacketPopBytes(&reader, data->mac, BLUETOOTH_MAC_SIZE);
    IoTPacketPopBytes(&reader, data->name, BT_SCAN_NAME_SIZE);
    data->rssi = (int16_t)IoTPacketPopUint16(&reader);
    data->paired = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}

size_t MakeBtOperationPacket(uint8_t *buffer, size_t size, uint16_t cmd)
{
    IoTPacketWriter writer;
    if(buffer == NULL || (cmd != REQ_BT_SCAN && cmd != REQ_BT_CONNECT_ALL)) return 0;
    IoTPacketBegin(&writer, buffer, size, cmd);
    return IoTPacketEnd(&writer);
}
size_t MakeBtOperationAckPacket(uint8_t *buffer, size_t size, uint16_t reqCmd, const BtOperationAckData *data)
{
    IoTPacketWriter writer;
    if(buffer == NULL || data == NULL || (reqCmd != REQ_BT_SCAN && reqCmd != REQ_BT_CONNECT_ALL)) return 0;
    IoTPacketBegin(&writer, buffer, size, REQ_TO_ACK(reqCmd));
    IoTPacketPushUint8(&writer, data->result);
    IoTPacketPushUint8(&writer, data->reason);
    return IoTPacketEnd(&writer);
}
int ReadBtOperationAckData(const uint8_t *payload, size_t length, BtOperationAckData *data)
{
    IoTPacketReader reader;
    if(payload == NULL || data == NULL || length != BT_OPERATION_ACK_SIZE) return -1;
    IoTPacketOpen(&reader, payload, length);
    data->result = IoTPacketPopUint8(&reader);
    data->reason = IoTPacketPopUint8(&reader);
    return IoTPacketCheckRead(&reader);
}
