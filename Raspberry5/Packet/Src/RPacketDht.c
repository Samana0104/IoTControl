#include "RPacketDht.h"
#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RLog.h"

#include <inttypes.h>
#include <string.h>

int RPacketDhtReceive(RSession *session, const uint8_t *payload, size_t length)
{
    DatabaseValue params[3];
    DhtData data;
    uint64_t affectedRows;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketDhtReceive: NULL argument");
        return -1;
    }
    if(!session->authenticated)
    {
        RLOG_WARN("[%s] DHT from unauthenticated session", session->label);
        return -1;
    }
    if(ReadDhtData(payload, length, &data) != 0)
    {
        RLOG_WARN("[%s] Malformed DHT payload: length=%zu", session->label, length);
        return -1;
    }
    RLOG_INFO("[%s] DHT: temp=%u, humi=%u", session->label, (unsigned int)data.temp, (unsigned int)data.humi);
    // DB 오류로 정상 연결을 끊지 않음
    params[0] = DATABASE_NUMBER(data.temp);
    params[1] = DATABASE_NUMBER(data.humi);
    params[2] = DATABASE_TEXT(session->memberId);
    if(ExecuteDatabaseQuery(QUERY_UPDATE_DHT, params, 3, NULL, NULL, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] DHT DB UPDATE failed: id=%s", session->label, session->memberId);
        return 0;
    }
    RLOG_INFO("[%s] DHT DB UPDATE: id=%s, affected=%" PRIu64, session->label, session->memberId, affectedRows);
    return 0;
}
