#include "RPacketFan.h"
#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RLog.h"
#include "RNetwork.h"

#include <inttypes.h>

#define FAN_MAX_PERCENT 100

int RPacketFanReceive(RSession *session, const uint8_t *payload, size_t length)
{
    DatabaseValue params[1];
    FanData data;
    uint64_t affectedRows;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketFanReceive: NULL argument");
        return -1;
    }
    if(ReadFanData(payload, length, &data) != 0)
    {
        RLOG_WARN("[%s] Malformed FAN payload: length=%zu", session->label, length);
        return -1;
    }
    RLOG_INFO("[%s] FAN: fanSpeed=%u", session->label, (unsigned int)data.fanSpeed);
    params[0] = DATABASE_NUMBER(data.fanSpeed);
    if(ExecuteDatabaseQuery(QUERY_UPDATE_FAN, params, 1, NULL, NULL, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] FAN DB UPDATE failed: singleton_id=1", session->label);
        return 0;
    }
    RLOG_INFO("[%s] FAN DB UPDATE: singleton_id=1, affected=%" PRIu64, session->label, affectedRows);
    return 0;
}

int RPacketFanReceiveAck(RSession *session, const uint8_t *payload, size_t length)
{
    ResultData result;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketFanReceiveAck: NULL argument");
        return -1;
    }
    if(ReadResultData(payload, length, &result) != 0)
    {
        RLOG_WARN("[%s] Malformed ACK_FAN payload: length=%zu", session->label, length);
        return -1;
    }
    if(result.result != RESULT_SUCCESS)
    {
        RLOG_WARN("[%s] FAN control rejected by device: id=%s, fd=%d, result=%u", session->label, session->memberId, session->fd, (unsigned int)result.result);
        return 0;
    }
    RLOG_INFO("[%s] FAN control applied: id=%s, fd=%d", session->label, session->memberId, session->fd);
    return 0;
}

int RPacketFanSetSpeed(int fd, uint8_t percent)
{
    FanData data = {.fanSpeed = percent};
    uint8_t frame[HEADER_SIZE + FAN_DATA_SIZE];
    size_t frameLength;
    int result;

    if(fd < 0 || percent > FAN_MAX_PERCENT)
    {
        RLOG_ERROR("RPacketFanSetSpeed: invalid argument: fd=%d, percent=%u", fd, (unsigned int)percent);
        return -1;
    }
    frameLength = MakeFanControlPacket(frame, sizeof(frame), &data);
    if(frameLength == 0)
    {
        RLOG_ERROR("REQ_FAN frame build failed");
        return -1;
    }

    result = RNetSend(fd, frame, frameLength);
    if(result == 1)
    {
        RLOG_WARN("FAN control skipped: fd=%d has no connected session", fd);
        return 1;
    }
    if(result != 0)
    {
        RLOG_WARN("FAN control send failed: fd=%d", fd);
        return -1;
    }
    RLOG_INFO("FAN control sent: fd=%d, speed=%u%%", fd, (unsigned int)percent);
    return 0;
}
