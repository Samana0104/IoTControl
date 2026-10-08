#include "SIotProtocol.h"
#include "IoTPacket.h"
#include "IoTPacketCodec.h"
#include "IoTPacketStream.h"
#include "SZS040.h"
#include <string.h>

// 프레임 중간에 바이트가 이 시간 이상 끊기면 버리고 다음 헤더부터 다시 받음.
#define SIOT_RECEIVE_TIMEOUT_MS 500U
// 깨진 프레임 뒤에는 줄이 이 시간 동안 조용해질 때까지 바이트를 버림 (매직 바이트 대신 재동기화).
#define SIOT_RESYNC_IDLE_MS 50U

typedef struct _SIotProtocol
{
    SIotPacketHandler packetHandler;
    SIotSendHandler sendHandler;
    bool initialized;

    uint8_t header[HEADER_SIZE];
    uint8_t headerLength;
    HeaderData receiveHeader;
    uint8_t receiveData[MAX_PAYLOAD_SIZE];
    uint16_t receivedLength;
    bool receivingPayload;
    bool discarding;
    uint32_t lastByteTick;

    uint8_t sendFrame[PACKET_FRAME_SIZE];
} SIotProtocol;

static SIotProtocol protocol;

static bool IsValidPacket(uint16_t cmd, uint16_t length)
{
    // 수신 버퍼(MAX_PAYLOAD_SIZE)보다 긴 프레임은 공용 규칙상 맞아도 받지 않음
    return length <= MAX_PAYLOAD_SIZE && CheckPacketLength(cmd, length) == 0;
}

static void ResetReceive(void)
{
    protocol.headerLength = 0;
    protocol.receivedLength = 0;
    protocol.receivingPayload = false;
}

// 깨진 프레임: 지금까지 받은 것을 버리고 줄이 조용해질 때까지 대기.
static void StartResync(void)
{
    ResetReceive();
    protocol.discarding = true;
}

static void FinishReceive(void)
{
    bool valid = CheckPacketCrc(protocol.header, &protocol.receiveHeader, protocol.receiveData) == 0;

    ResetReceive();
    if (!valid)
    {
        StartResync();
        return;
    }
    if (protocol.packetHandler != NULL)
    {
        protocol.packetHandler(protocol.receiveHeader.cmd, protocol.receiveData, protocol.receiveHeader.length);
    }
}

static void ProcessHeader(void)
{
    DecodePacketHeader(protocol.header, &protocol.receiveHeader);
    if (!IsValidPacket(protocol.receiveHeader.cmd, protocol.receiveHeader.length))
    {
        StartResync();
        return;
    }
    if (protocol.receiveHeader.length == 0)
    {
        FinishReceive();
        return;
    }
    protocol.receivingPayload = true;
}

static void ProcessByte(uint8_t byte)
{
    if (protocol.discarding)
    {
        return;
    }
    if (protocol.receivingPayload)
    {
        protocol.receiveData[protocol.receivedLength++] = byte;
        if (protocol.receivedLength == protocol.receiveHeader.length)
        {
            FinishReceive();
        }
        return;
    }

    protocol.header[protocol.headerLength++] = byte;
    if (protocol.headerLength == HEADER_SIZE)
    {
        ProcessHeader();
    }
}

void SIotProtocolInit(SIotPacketHandler packetHandler, SIotSendHandler sendHandler)
{
    memset(&protocol, 0, sizeof(protocol));
    protocol.packetHandler = packetHandler;
    protocol.sendHandler = sendHandler;
    protocol.initialized = true;
}

bool SIotProtocolIsBusy(void)
{
    return protocol.receivingPayload || protocol.headerLength != 0 || protocol.discarding;
}

bool SIotProtocolSendPacket(uint16_t cmd, const void *data, uint16_t length)
{
    IoTPacketWriter writer;
    size_t frameLength;
    bool success;

    if (!protocol.initialized || SIotProtocolIsBusy() || !IsValidPacket(cmd, length) ||
        (data == NULL && length > 0))
    {
        return false;
    }

    IoTPacketBegin(&writer, protocol.sendFrame, sizeof(protocol.sendFrame), cmd);
    if (length > 0)
    {
        IoTPacketPushBytes(&writer, data, length);
    }
    frameLength = IoTPacketEnd(&writer);
    success = frameLength > 0 && SZS040Write(protocol.sendFrame, (uint16_t)frameLength);
    if (protocol.sendHandler != NULL)
    {
        protocol.sendHandler(cmd, success);
    }
    return success;
}

void SIotProtocolUpdate(void)
{
    uint8_t byte;
    uint32_t now;

    if (!protocol.initialized)
    {
        return;
    }
    now = HAL_GetTick();
    while (SZS040ReadByte(&byte))
    {
        ProcessByte(byte);
        protocol.lastByteTick = now;
    }

    now = HAL_GetTick();
    if (protocol.discarding)
    {
        if (now - protocol.lastByteTick >= SIOT_RESYNC_IDLE_MS)
        {
            protocol.discarding = false;
        }
    }
    else if ((protocol.headerLength != 0 || protocol.receivingPayload) &&
             now - protocol.lastByteTick >= SIOT_RECEIVE_TIMEOUT_MS)
    {
        ResetReceive();
    }
}
