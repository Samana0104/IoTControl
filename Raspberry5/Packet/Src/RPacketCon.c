#include "RPacketCon.h"
#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RLog.h"

#include <inttypes.h>

int RPacketConReceive(RSession *session, const uint8_t *payload, size_t length)
{
    DatabaseValue params[1];
    ConData data;
    uint64_t affectedRows;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketConReceive: NULL argument");
        return -1;
    }
    if(ReadConData(payload, length, &data) != 0)
    {
        RLOG_WARN("[%s] Malformed CON payload: length=%zu", session->label, length);
        return -1;
    }
    RLOG_INFO("[%s] CON: tempData=%u", session->label, (unsigned int)data.tempData);
    params[0] = DATABASE_NUMBER(data.tempData);
    if(ExecuteDatabaseQuery(QUERY_UPDATE_CON, params, 1, NULL, NULL, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] CON DB UPDATE failed: singleton_id=1", session->label);
        return 0;
    }
    RLOG_INFO("[%s] CON DB UPDATE: singleton_id=1, affected=%" PRIu64, session->label, affectedRows);
    return 0;
}
