#include "RPacketCon.h"
#include "IoTPacketCodec.h"
#include "RDatabase.h"
#include "RLog.h"

#include <inttypes.h>

int RPacketConReceive(RSession *session, const uint8_t *payload, size_t length)
{
    const char *label = RSessionGetLabel(session);
    ConData data;
    uint64_t affectedRows;

    if(ReadConData(payload, length, &data) != 0)
    {
        return -1;
    }
    RLOG_INFO("[%s] CON: tempData=%u", label, (unsigned int)data.tempData);
    if(UpdateConData(&data, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] CON DB UPDATE failed: singleton_id=1", label);
    }
    else
    {
        RLOG_INFO("[%s] CON DB UPDATE: singleton_id=1, affected=%" PRIu64, label, affectedRows);
    }
    return 0;
}
