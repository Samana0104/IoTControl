#include "RPacketDhtRefresh.h"
#include "IoTPacketCodec.h"
#include "RLog.h"
#include "RNetwork.h"
#include <pthread.h>
#include <string.h>
#include <time.h>

#ifndef DHT_REFRESH_TIMEOUT_MS
#define DHT_REFRESH_TIMEOUT_MS 5000
#endif

typedef struct _RDhtRefresh
{
    int active;
    char memberId[MEM_ID_SIZE + 1];
    RNetReference device;
    RNetReference requester;
    uint64_t deadlineMs;
} RDhtRefresh;

static pthread_mutex_t refreshMutex = PTHREAD_MUTEX_INITIALIZER;
static RDhtRefresh requests[MAX_SESSION];
static uint64_t GetMonotonicMs(void);
static int SendReply(const RNetReference *requester, uint8_t reason);
static uint8_t StartRequest(const RSessionSnapshot *device, const RNetReference *requester);
static void FinishRequest(RDhtRefresh *request, uint8_t reason);

int RPacketDhtRefreshReceive(RSession *session, const uint8_t *payload, size_t length)
{
    DhtRefreshData data;
    char memberId[MEM_ID_SIZE + 1];
    RSessionSnapshot actor;
    RSessionSnapshot snapshots[MAX_SESSION];
    RSessionSnapshot *device = NULL;
    RNetReference requester;
    size_t count;
    uint8_t reason;

    if (session == NULL || ReadDhtRefreshData(payload, length, &data) != 0)
        return -1;
    if (RSessionFindByFd(session->fd, &actor) != 0 || RNetGetSessionReference(&actor, &requester) != 0)
        return -1;
    if (session->type != SESSION_TCP || !session->authenticated || session->memberType != MEMBER_TYPE_PC)
        return SendReply(&requester, DHT_REFRESH_NOT_ALLOWED);
    memcpy(memberId, data.id, MEM_ID_SIZE);
    memberId[MEM_ID_SIZE] = '\0';
    if (memberId[0] == '\0')
        return SendReply(&requester, DHT_REFRESH_INVALID_TARGET);
    for (size_t index = strlen(memberId); index < MEM_ID_SIZE; ++index)
        if (data.id[index] != '\0')
            return SendReply(&requester, DHT_REFRESH_INVALID_TARGET);
    count = RSessionGetSnapshots(snapshots);
    for (size_t index = 0; index < count; ++index)
    {
        RSessionSnapshot *candidate = &snapshots[index];
        if (!candidate->authenticated || (candidate->memberType != MEMBER_TYPE_STM32 && candidate->memberType != MEMBER_TYPE_ARDUINO) || strcmp(candidate->memberId, memberId) != 0)
            continue;
        if (device == NULL || (candidate->type == SESSION_BLUETOOTH && device->type != SESSION_BLUETOOTH))
            device = candidate;
    }
    if (device == NULL)
        return SendReply(&requester, DHT_REFRESH_NOT_CONNECTED);
    pthread_mutex_lock(&refreshMutex);
    reason = StartRequest(device, &requester);
    pthread_mutex_unlock(&refreshMutex);
    return reason == DHT_REFRESH_OK ? 0 : SendReply(&requester, reason);
}

int RPacketDhtRequestDevice(const RSessionSnapshot *device)
{
    RNetReference requester = {.fd = -1};
    uint8_t reason;
    if (device == NULL || !device->authenticated || (device->memberType != MEMBER_TYPE_STM32 && device->memberType != MEMBER_TYPE_ARDUINO))
        return 1;
    pthread_mutex_lock(&refreshMutex);
    reason = StartRequest(device, &requester);
    pthread_mutex_unlock(&refreshMutex);
    return reason == DHT_REFRESH_OK ? 0 : 1;
}

void RPacketDhtRefreshHandleAck(const RSession *session, uint8_t result, int saved)
{
    RSessionSnapshot snapshot;
    RNetReference device;
    if (session == NULL || RSessionFindByFd(session->fd, &snapshot) != 0 || RNetGetSessionReference(&snapshot, &device) != 0)
        return;
    pthread_mutex_lock(&refreshMutex);
    for (size_t index = 0; index < MAX_SESSION; ++index)
    {
        RDhtRefresh *request = &requests[index];
        if (!request->active || request->device.fd != device.fd || request->device.token != device.token)
            continue;
        if (GetMonotonicMs() >= request->deadlineMs)
        {
            RNetCloseReferenced(&request->device);
            FinishRequest(request, DHT_REFRESH_TIMEOUT);
        }
        else
            FinishRequest(request, result != RESULT_SUCCESS ? DHT_REFRESH_REJECTED : saved ? DHT_REFRESH_OK
                                                                                           : DHT_REFRESH_DB_ERROR);
        break;
    }
    pthread_mutex_unlock(&refreshMutex);
}

void RPacketDhtRefreshTick(uint64_t nowMs)
{
    pthread_mutex_lock(&refreshMutex);
    for (size_t index = 0; index < MAX_SESSION; ++index)
    {
        RDhtRefresh *request = &requests[index];
        if (!request->active)
            continue;
        if (!RNetIsReferenceOpen(&request->device))
            FinishRequest(request, DHT_REFRESH_DISCONNECTED);
        else if (nowMs >= request->deadlineMs)
        {
            // 기존 ACK_DHT에는 요청 번호가 없음. 지연 응답을 다음 요청과
            // 혼동하지 않도록 연결 종료.
            RNetCloseReferenced(&request->device);
            FinishRequest(request, DHT_REFRESH_TIMEOUT);
        }
    }
    pthread_mutex_unlock(&refreshMutex);
}

void RPacketDhtRefreshReset(void)
{
    pthread_mutex_lock(&refreshMutex);
    memset(requests, 0, sizeof(requests));
    pthread_mutex_unlock(&refreshMutex);
}

static uint64_t GetMonotonicMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static int SendReply(const RNetReference *requester, uint8_t reason)
{
    DhtRefreshAckData ack = {.result = reason == DHT_REFRESH_OK ? RESULT_SUCCESS : RESULT_FAIL, .reason = reason};
    uint8_t frame[HEADER_SIZE + DHT_REFRESH_ACK_DATA_SIZE];
    size_t frameLength = MakeDhtRefreshAckPacket(frame, sizeof(frame), &ack);
    if (requester->fd < 0)
        return 0;
    return frameLength > 0 && RNetSendReferenced(requester, frame, frameLength) == 0 ? 0 : -1;
}

// refreshMutex 아래에서 등록 후 송신. 전체/CLI/개별 요청도 같은 ID의 ACK 대기를
// 공유.
static uint8_t StartRequest(const RSessionSnapshot *device, const RNetReference *requester)
{
    RDhtRefresh *slot = NULL;
    RNetReference reference;
    uint8_t frame[HEADER_SIZE];
    size_t frameLength = MakeDhtRequestPacket(frame, sizeof(frame));
    for (size_t index = 0; index < MAX_SESSION; ++index)
    {
        if (requests[index].active && strcmp(requests[index].memberId, device->memberId) == 0)
            return DHT_REFRESH_BUSY;
        if (!requests[index].active && slot == NULL)
            slot = &requests[index];
    }
    if (slot == NULL)
        return DHT_REFRESH_BUSY;
    if (RNetGetSessionReference(device, &reference) != 0)
        return DHT_REFRESH_NOT_CONNECTED;
    if (frameLength == 0)
        return DHT_REFRESH_SEND_FAILED;
    slot->active = 1;
    slot->device = reference;
    slot->requester = *requester;
    slot->deadlineMs = GetMonotonicMs() + DHT_REFRESH_TIMEOUT_MS;
    memcpy(slot->memberId, device->memberId, sizeof(slot->memberId));
    if (RNetSendReferenced(&slot->device, frame, frameLength) != 0)
    {
        slot->active = 0;
        return DHT_REFRESH_SEND_FAILED;
    }
    RLOG_INFO("DHT refresh sent: id=%s, requester_fd=%d", slot->memberId, requester->fd);
    return DHT_REFRESH_OK;
}

static void FinishRequest(RDhtRefresh *request, uint8_t reason)
{
    RLOG_INFO("DHT refresh finished: id=%s, reason=%u", request->memberId, (unsigned int)reason);
    if (SendReply(&request->requester, reason) != 0)
        RLOG_WARN("DHT refresh result not delivered: requester disconnected");
    request->active = 0;
}
