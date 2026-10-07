#include "RCtrlDht.h"
#include "IoTPacketCodec.h"
#include "RDatabase.h"
#include "RLog.h"

#include <inttypes.h>
#include <string.h>

int RCtrlDhtReceive(const RCtrlContext *context, const uint8_t *payload, size_t length)
{
    DhtData data;
    uint64_t affectedRows;

    if(ReadDhtData(payload, length, &data) != 0)
    {
        return -1;
    }
    RLOG_INFO("[%s] DHT: temp=%u, humi=%u", context->label, (unsigned int)data.temp, (unsigned int)data.humi);
    /* DB errors must not tear down an otherwise valid TCP/BT connection. */
    if(UpdateDhtData(context->memberId, strlen(context->memberId), &data, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] DHT DB UPDATE failed: id=%s", context->label, context->memberId);
    }
    else
    {
        RLOG_INFO("[%s] DHT DB UPDATE: id=%s, affected=%" PRIu64, context->label, context->memberId, affectedRows);
    }
    return 0;
}
