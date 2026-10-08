// Raspberry5/Packet/Src/RPacketFan.c 전체 교체 예제입니다. 서버 원본은 수정하지 않았습니다.
#include "RPacketFan.h"
#include "RPacket.h"
#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RLog.h"
#include "RNetwork.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define FAN_MAX_PERCENT 100
// fan.speed INT 문자열 최대 길이 (부호 + 10자리)
#define FAN_SPEED_TEXT_SIZE 16

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

int RPacketFanQueryReceive(RSession *session, const uint8_t *payload, size_t length)
{
    FanQueryAckData ack = {.result = RESULT_FAIL};
    uint8_t frame[HEADER_SIZE + FAN_QUERY_ACK_DATA_SIZE];
    char speedText[FAN_SPEED_TEXT_SIZE];
    size_t frameLength;
    int queryResult;

    (void)payload;
    (void)length;
    if(session == NULL)
    {
        RLOG_ERROR("RPacketFanQueryReceive: NULL session");
        return -1;
    }

    queryResult = QueryDatabaseValue(QUERY_SELECT_FAN, NULL, 0, speedText, sizeof(speedText));
    if(queryResult == 1)
    {
        char *end;
        long speed;

        errno = 0;
        speed = strtol(speedText, &end, 10);
        if(errno == 0 && end != speedText && *end == '\0' && speed >= 0 && speed <= FAN_MAX_PERCENT)
        {
            ack.result = RESULT_SUCCESS;
            ack.fan.fanSpeed = (uint16_t)speed;
        }
        else
        {
            RLOG_WARN("[%s] FAN query: invalid speed in DB: %s", session->label, speedText);
        }
    }
    else
    {
        RLOG_WARN("[%s] FAN query failed: reason=%s", session->label, queryResult == 0 ? "no fan row (singleton_id=1)" : "database error");
    }

    frameLength = MakeFanQueryAckPacket(frame, sizeof(frame), &ack);
    if(frameLength == 0)
    {
        RLOG_ERROR("[%s] ACK_FAN_QUERY frame build failed", session->label);
        return -1;
    }
    if(RNetSend(session->fd, frame, frameLength) != 0)
    {
        RLOG_WARN("[%s] ACK_FAN_QUERY send failed", session->label);
        return -1;
    }
    RLOG_INFO("[%s] FAN query: result=%s, speed=%u", session->label, ack.result == RESULT_SUCCESS ? "success" : "fail", (unsigned int)ack.fan.fanSpeed);
    return 0;
}

// PC -> 서버 REQ_FAN_UPDATE: 기존 fan 단일 행 UPDATE 후 저장값을 확인하여 ACK 응답.
int RPacketFanUpdateReceive(RSession *session, const uint8_t *payload, size_t length)
{
    FanData data;
    char memberId[MEM_ID_SIZE + 1];
    char memberType[8];
    char speedText[FAN_SPEED_TEXT_SIZE];
    DatabaseValue memberParam;
    DatabaseValue speedParam;
    char *end;
    long storedSpeed;

    if (session == NULL || payload == NULL || session->type != SESSION_TCP || !session->authenticated || ReadFanData(payload, length, &data) != 0 || data.fanSpeed > FAN_MAX_PERCENT)
        return -1;
    memcpy(memberId, session->memberId, MEM_ID_SIZE);
    memberId[MEM_ID_SIZE] = '\0';
    memberParam = DATABASE_TEXT(memberId);
    if (QueryDatabaseValue(QUERY_SELECT_MEMBER_TYPE, &memberParam, 1, memberType, sizeof(memberType)) != 1 || strcmp(memberType, "PC") != 0)
        return RPacketSendAck(session, REQ_FAN_UPDATE, 0);

    speedParam = DATABASE_NUMBER(data.fanSpeed);
    if (ExecuteDatabaseQuery(QUERY_UPDATE_FAN, &speedParam, 1, NULL, NULL, NULL) != 0 || QueryDatabaseValue(QUERY_SELECT_FAN, NULL, 0, speedText, sizeof(speedText)) != 1)
        return RPacketSendAck(session, REQ_FAN_UPDATE, 0);

    // 같은 값 UPDATE는 변경 행 수가 0일 수 있습니다. SELECT로 행 존재와 저장값을 확인합니다.
    errno = 0;
    storedSpeed = strtol(speedText, &end, 10);
    return RPacketSendAck(session, REQ_FAN_UPDATE, errno == 0 && end != speedText && *end == '\0' && storedSpeed == data.fanSpeed);
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