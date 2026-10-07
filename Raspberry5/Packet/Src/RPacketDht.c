#include "RPacketDht.h"
#include "IoTPacketCodec.h"
#include "RDatabase.h"
#include "RLog.h"

#include <inttypes.h>
#include <string.h>

int RPacketDhtReceive(RSession *session, const uint8_t *payload, size_t length)
{
    const char *label = RSessionGetLabel(session);
    char memberId[MEM_ID_SIZE + 1];
    DhtData data;
    uint64_t affectedRows;

    if(RSessionGetMemberId(session, memberId) != 0 || ReadDhtData(payload, length, &data) != 0)
    {
        return -1;
    }
    RLOG_INFO("[%s] DHT: temp=%u, humi=%u", label, (unsigned int)data.temp, (unsigned int)data.humi);
    // DB 오류로 정상 연결을 끊지 않음
    if(UpdateDhtData(memberId, strlen(memberId), &data, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] DHT DB UPDATE failed: id=%s", label, memberId);
    }
    else
    {
        RLOG_INFO("[%s] DHT DB UPDATE: id=%s, affected=%" PRIu64, label, memberId, affectedRows);
    }
    return 0;
}
