#include "RCtrlFan.h"
#include "IoTPacketCodec.h"
#include "RDatabase.h"
#include "RLog.h"
#include "RSession.h"

#include <inttypes.h>

#define FAN_MAX_PERCENT 100

int RCtrlFanReceive(const RCtrlContext *context, const uint8_t *payload, size_t length)
{
    FanData data;
    uint64_t affectedRows;

    if(ReadFanData(payload, length, &data) != 0)
    {
        return -1;
    }
    RLOG_INFO("[%s] FAN: fanSpeed=%u", context->label, (unsigned int)data.fanSpeed);
    if(UpdateFanData(&data, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] FAN DB UPDATE failed: singleton_id=1", context->label);
    }
    else
    {
        RLOG_INFO("[%s] FAN DB UPDATE: singleton_id=1, affected=%" PRIu64, context->label, affectedRows);
    }
    return 0;
}

int RCtrlFanReceiveAck(const RCtrlContext *context, const uint8_t *payload, size_t length)
{
    ResultData result;

    if(ReadResultData(payload, length, &result) != 0)
    {
        return -1;
    }
    if(result.result == RESULT_SUCCESS)
    {
        RLOG_INFO("[%s] FAN control applied: id=%s, fd=%d", context->label, context->memberId, context->fd);
    }
    else
    {
        RLOG_WARN("[%s] FAN control rejected by device: id=%s, fd=%d, result=%u", context->label, context->memberId, context->fd, (unsigned int)result.result);
    }
    return 0;
}

int RCtrlFanSetSpeed(int fd, uint8_t percent)
{
    FanData data = {.fanSpeed = percent};
    uint8_t frame[HEADER_SIZE + FAN_DATA_SIZE];
    int result;

    if(fd < 0 || percent > FAN_MAX_PERCENT)
    {
        return -1;
    }
    result = RSessionSend(fd, frame, MakeFanControlPacket(frame, sizeof(frame), &data));
    if(result == 0)
    {
        RLOG_INFO("FAN control sent: fd=%d, speed=%u%%", fd, (unsigned int)percent);
    }
    else if(result == 1)
    {
        RLOG_WARN("FAN control skipped: fd=%d has no connected session", fd);
    }
    else
    {
        RLOG_WARN("FAN control send failed: fd=%d", fd);
    }
    return result;
}
