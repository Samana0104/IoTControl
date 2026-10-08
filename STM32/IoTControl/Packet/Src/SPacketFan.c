#include "SPacketFan.h"
#include "SFan.h"
#include "SIotProtocol.h"
#include "SLog.h"
#include "IoTPacketCodec.h"

void SPacketFanRequestReceive(const uint8_t *payload, uint16_t length)
{
    FanData fanData;
    uint8_t result = RESULT_FAIL;

    if (ReadFanData(payload, length, &fanData) == 0 && fanData.fanSpeed <= SFAN_MAX_PERCENT)
    {
        SFanSetSpeed((uint8_t)fanData.fanSpeed);
        result = RESULT_SUCCESS;
        SLOG_INFO("bt fan control: speed=%u%%", (unsigned int)fanData.fanSpeed);
    }
    else
    {
        SLOG_WARN("bt fan control rejected: length=%u", (unsigned int)length);
    }
    SIotProtocolSendPacket(ACK_FAN, &result, RESULT_DATA_SIZE);
}
