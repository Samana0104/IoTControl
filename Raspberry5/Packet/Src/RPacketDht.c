#include "RPacketDht.h"
#include "RPacketDhtRefresh.h"
#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RLog.h"
#include "RNetwork.h"
#include "RPacket.h"

#include <inttypes.h>
#include <string.h>

static int SaveDhtData(const RSession *session, const DhtData *data);
static int IsDhtDevice(const RSessionSnapshot *snapshot);

int RPacketDhtReceive(RSession *session, const uint8_t *payload, size_t length)
{
    DhtData data;

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
    SaveDhtData(session, &data);
    return 0;
}

int RPacketDhtReceiveAck(RSession *session, const uint8_t *payload, size_t length)
{
    DhtAckData ack;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketDhtReceiveAck: NULL argument");
        return -1;
    }
    if(!session->authenticated)
    {
        RLOG_WARN("[%s] ACK_DHT from unauthenticated session", session->label);
        return -1;
    }
    if(ReadDhtAckData(payload, length, &ack) != 0)
    {
        RLOG_WARN("[%s] Malformed ACK_DHT payload: length=%zu", session->label, length);
        return -1;
    }
    if(ack.result != RESULT_SUCCESS)
    {
        RPacketDhtRefreshHandleAck(session, ack.result, 0);
        RLOG_WARN("[%s] DHT read failed on device: id=%s, result=%u", session->label, session->memberId, (unsigned int)ack.result);
        return 0;
    }
    const int SAVED = SaveDhtData(session, &ack.dht) == 0;
    RPacketDhtRefreshHandleAck(session, ack.result, SAVED);
    return 0;
}

int RPacketDhtCollectReceive(RSession *session, const uint8_t *payload, size_t length)
{
    int sentCount;

    (void)payload;
    (void)length;
    if(session == NULL)
    {
        RLOG_ERROR("RPacketDhtCollectReceive: NULL session");
        return -1;
    }
    sentCount = RPacketDhtRequestAll(session->fd);
    RLOG_INFO("[%s] DHT collect: id=%s, devices=%d", session->label, session->memberId, sentCount);
    return RPacketSendAck(session, REQ_DHT_COLLECT, sentCount > 0);
}

int RPacketDhtRequestAll(int excludeFd)
{
    RSessionSnapshot snapshots[MAX_SESSION];
    size_t snapshotCount = RSessionGetSnapshots(snapshots);
    int sentCount = 0;

    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const RSessionSnapshot *snapshot = &snapshots[index];

        if(snapshot->fd == excludeFd || !IsDhtDevice(snapshot))
        {
            continue;
        }
        if(RPacketDhtRequestDevice(snapshot) != 0)
        {
            RLOG_WARN("REQ_DHT send failed: id=%s, fd=%d", snapshot->memberId, snapshot->fd);
            continue;
        }
        ++sentCount;
    }
    RLOG_INFO("REQ_DHT sent: devices=%d", sentCount);
    return sentCount;
}

// DB 오류로 정상 연결을 끊지 않음
static int SaveDhtData(const RSession *session, const DhtData *data)
{
    DatabaseValue params[3];
    uint64_t insertedRows;

    RLOG_INFO("[%s] DHT: temp=%u, humi=%u", session->label, (unsigned int)data->temp, (unsigned int)data->humi);
    params[0] = DATABASE_TEXT(session->memberId);
    params[1] = DATABASE_NUMBER(data->temp);
    params[2] = DATABASE_NUMBER(data->humi);
    if(ExecuteDatabaseQuery(QUERY_INSERT_DHT, params, 3, NULL, NULL, &insertedRows) != 0)
    {
        RLOG_WARN("[%s] DHT DB INSERT failed: id=%s", session->label, session->memberId);
        return -1;
    }
    RLOG_INFO("[%s] DHT DB INSERT: id=%s, inserted=%" PRIu64, session->label, session->memberId, insertedRows);
    return insertedRows == 1 ? 0 : -1;
}

// 인증된 STM32/Arduino만 현장 측정 요청 대상으로 사용
static int IsDhtDevice(const RSessionSnapshot *snapshot)
{
    if(!snapshot->authenticated)
    {
        return 0;
    }
    return snapshot->memberType == MEMBER_TYPE_STM32 || snapshot->memberType == MEMBER_TYPE_ARDUINO;
}
