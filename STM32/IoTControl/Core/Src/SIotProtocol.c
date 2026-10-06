#include "SIotProtocol.h"
#include "IoTPacket.h"
#include <string.h>

#define SIOT_REQUEST_RETRY_MS 5000U
#define SIOT_SEND_TIMEOUT_MS 15000U

typedef struct _SIotProtocol
{
    SIotProtocolIo io;
    SIotPacketHandler packetHandler;
    SIotSendHandler sendHandler;
    bool initialized;
    uint8_t header[HEADER_SIZE];
    uint8_t headerLength;
    uint32_t headerTick;
    uint8_t sendData[MAX_MESSAGE_SIZE];
    uint8_t sendCmd;
    uint8_t sendLength;
    uint32_t sendTick;
    bool sendPending;
    bool requestReady;
    uint8_t receiveData[MAX_MESSAGE_SIZE];
    uint8_t receiveCmd;
    uint8_t receiveLength;
    uint8_t receivedLength;
    uint32_t receiveTick;
    bool receivePending;
} SIotProtocol;

static SIotProtocol protocol;

static bool IsValidPacket(uint8_t cmd, uint16_t length)
{
    switch (cmd)
    {
        case CMD_DHT11_DATA: return length == sizeof(DhtData);
        case CMD_FAN_DATA: return length == sizeof(FanData);
        case CMD_CON_DATA: return length == sizeof(ConData);
        case CMD_MEM_DATA: return length == sizeof(MemData);
        case CMD_CHAT_DATA: return length <= MAX_MESSAGE_SIZE;
        case CMD_BLUETOOTH_REGISTER: return length == sizeof(BluetoothRegisterData);
        default: return false;
    }
}

static bool SendRequest(uint8_t cmd, uint8_t retry)
{
    const uint8_t header[HEADER_SIZE] = {HEADER_REQUEST_0, HEADER_REQUEST_1, cmd, retry};
    return protocol.io.write(header, sizeof(header));
}

static void FinishSend(bool success)
{
    uint8_t cmd = protocol.sendCmd;
    protocol.sendPending = false;
    protocol.requestReady = false;
    if (protocol.sendHandler != NULL)
    {
        protocol.sendHandler(cmd, success);
    }
}

static void FinishReceive(void)
{
    protocol.receivePending = false;
    if (protocol.packetHandler != NULL)
    {
        protocol.packetHandler(protocol.receiveCmd, protocol.receiveData, protocol.receiveLength);
    }
}

static void ProcessHeader(void)
{
    uint8_t cmd = protocol.header[2];
    uint8_t length = protocol.header[3];

    if (protocol.header[0] == HEADER_REQUEST_0)
    {
        // 이전 DATA에 대한 남은 재전송 RQ는 cmd 검사 전에 버림.
        if (length == 1 || !protocol.sendPending)
        {
            return;
        }
        if (length != 0 || cmd != protocol.sendCmd)
        {
            FinishSend(false);
            return;
        }
        protocol.requestReady = true;
        return;
    }

    if (!IsValidPacket(cmd, length) || !SendRequest(cmd, 0))
    {
        return;
    }
    protocol.receiveCmd = cmd;
    protocol.receiveLength = length;
    protocol.receivedLength = 0;
    protocol.receiveTick = protocol.io.getTick();
    protocol.receivePending = true;
    if (length == 0)
    {
        FinishReceive();
    }
}

static void ProcessByte(uint8_t byte)
{
    if (protocol.receivePending)
    {
        protocol.receiveData[protocol.receivedLength++] = byte;
        protocol.receiveTick = protocol.io.getTick();
        if (protocol.receivedLength == protocol.receiveLength)
        {
            FinishReceive();
        }
        return;
    }

    if (protocol.headerLength == 0)
    {
        if (byte != HEADER_OK_0 && byte != HEADER_REQUEST_0)
        {
            return;
        }
    }
    else if (protocol.headerLength == 1)
    {
        uint8_t expected = protocol.header[0] == HEADER_OK_0 ? HEADER_OK_1 : HEADER_REQUEST_1;
        if (byte != expected)
        {
            protocol.headerLength = 0;
            // 새 헤더의 첫 바이트라면 보존.
            if (byte != HEADER_OK_0 && byte != HEADER_REQUEST_0)
            {
                return;
            }
        }
    }

    protocol.header[protocol.headerLength++] = byte;
    protocol.headerTick = protocol.io.getTick();
    if (protocol.headerLength == HEADER_SIZE)
    {
        protocol.headerLength = 0;
        ProcessHeader();
    }
}

void SIotProtocolInit(const SIotProtocolIo *io, SIotPacketHandler packetHandler, SIotSendHandler sendHandler)
{
    memset(&protocol, 0, sizeof(protocol));
    if (io == NULL || io->readByte == NULL || io->write == NULL || io->getTick == NULL)
    {
        return;
    }
    protocol.io = *io;
    protocol.packetHandler = packetHandler;
    protocol.sendHandler = sendHandler;
    protocol.initialized = true;
}

bool SIotProtocolIsBusy(void)
{
    return protocol.sendPending || protocol.receivePending || protocol.headerLength != 0;
}

bool SIotProtocolSendPacket(uint8_t cmd, const void *data, uint16_t length)
{
    if (!protocol.initialized || SIotProtocolIsBusy() || !IsValidPacket(cmd, length) ||
        (data == NULL && length > 0))
    {
        return false;
    }
    if (length > 0)
    {
        memcpy(protocol.sendData, data, length);
    }
    protocol.sendCmd = cmd;
    protocol.sendLength = (uint8_t)length;
    protocol.sendTick = protocol.io.getTick();
    protocol.sendPending = true;
    protocol.requestReady = false;

    const uint8_t header[HEADER_SIZE] = {HEADER_OK_0, HEADER_OK_1, cmd, (uint8_t)length};
    if (!protocol.io.write(header, sizeof(header)))
    {
        protocol.sendPending = false;
        return false;
    }
    return true;
}

void SIotProtocolUpdate(void)
{
    if (!protocol.initialized)
    {
        return;
    }
    uint8_t byte;
    while (protocol.io.readByte(&byte))
    {
        ProcessByte(byte);
    }

    uint32_t now = protocol.io.getTick();
    if (protocol.sendPending && now - protocol.sendTick >= SIOT_SEND_TIMEOUT_MS)
    {
        FinishSend(false);
    }
    // 최초 RQ 뒤에 이미 도착한 재전송 RQ를 전부 소비한 후 DATA 전송.
    else if (protocol.sendPending && protocol.requestReady && protocol.headerLength == 0)
    {
        bool success = protocol.io.write(protocol.sendData, protocol.sendLength);
        FinishSend(success);
    }

    if (protocol.receivePending && now - protocol.receiveTick >= SIOT_REQUEST_RETRY_MS)
    {
        if (!SendRequest(protocol.receiveCmd, 1))
        {
            protocol.receivePending = false;
        }
        protocol.receiveTick = protocol.io.getTick();
    }
    if (protocol.headerLength > 0 && now - protocol.headerTick >= SIOT_REQUEST_RETRY_MS)
    {
        protocol.headerLength = 0;
    }
}
