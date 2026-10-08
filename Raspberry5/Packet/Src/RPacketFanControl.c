#include "RPacketFanControl.h"
#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RNetwork.h"
#include "RLog.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FAN_CONTROL_TIMEOUT_MS
#define FAN_CONTROL_TIMEOUT_MS 5000
#endif
#define FAN_MAX_PERCENT 100
#define FAN_SPEED_TEXT_SIZE 16

// fan은 단일 장치. CLI와 PC 요청 모두 ACK_FAN 대기를 한 번에 하나로 제한.
typedef struct _RFanControl
{
    int active;
    RNetReference device;
    RNetReference requester; // fd=-1이면 CLI 요청
    char memberId[MEM_ID_SIZE + 1];
    uint16_t percent;
    uint64_t deadlineMs;
} RFanControl;

static pthread_mutex_t controlMutex = PTHREAD_MUTEX_INITIALIZER;
static RFanControl control;

static uint64_t GetMonotonicMs(void);
static int SendReply(const RNetReference *requester, uint8_t reason, uint16_t percent);
static uint8_t StartControl(const RSessionSnapshot *device, const RNetReference *requester, uint16_t percent);
static void FinishControl(uint8_t reason);

int RPacketFanApplyReceive(RSession *session, const uint8_t *payload, size_t length)
{
    FanApplyData data;
    RSessionSnapshot requesterSession;
    RSessionSnapshot snapshots[MAX_SESSION];
    RSessionSnapshot *device = NULL;
    RNetReference requester;
    char memberId[MEM_ID_SIZE + 1];
    char speedText[FAN_SPEED_TEXT_SIZE];
    char *end;
    long speed;
    uint8_t reason;
    size_t count;

    if(session == NULL || payload == NULL || ReadFanApplyData(payload, length, &data) != 0)
        return -1;
    if(RSessionFindByFd(session->fd, &requesterSession) != 0 || RNetGetSessionReference(&requesterSession, &requester) != 0)
        return -1;
    if(session->type != SESSION_TCP || !session->authenticated || session->memberType != MEMBER_TYPE_PC)
        return SendReply(&requester, FAN_APPLY_NOT_ALLOWED, 0);
    memcpy(memberId, data.id, MEM_ID_SIZE);
    memberId[MEM_ID_SIZE] = '\0';
    if(memberId[0] == '\0' || data.fan.fanSpeed > FAN_MAX_PERCENT)
        return SendReply(&requester, FAN_APPLY_INVALID_TARGET, 0);
    // NUL 뒤에는 패딩 0만 허용: 잘못된 고정 폭 ID를 다른 ID로 해석하지 않음.
    for(size_t index = strlen(memberId); index < MEM_ID_SIZE; ++index)
        if(data.id[index] != '\0')
            return SendReply(&requester, FAN_APPLY_INVALID_TARGET, 0);
    count = RSessionGetSnapshots(snapshots);
    for(size_t index = 0; index < count; ++index)
    {
        RSessionSnapshot *candidate = &snapshots[index];
        if(candidate->authenticated && candidate->type == SESSION_BLUETOOTH && candidate->memberType == MEMBER_TYPE_STM32 && strcmp(candidate->memberId, memberId) == 0)
        {
            if(device != NULL)
                return SendReply(&requester, FAN_APPLY_INVALID_TARGET, 0);
            device = candidate;
        }
    }
    if(device == NULL)
        return SendReply(&requester, FAN_APPLY_NOT_CONNECTED, data.fan.fanSpeed);
    if(QueryDatabaseValue(QUERY_SELECT_FAN, NULL, 0, speedText, sizeof(speedText)) != 1)
        return SendReply(&requester, FAN_APPLY_DB_ERROR, 0);
    errno = 0;
    speed = strtol(speedText, &end, 10);
    if(errno != 0 || end == speedText || *end != '\0' || speed < 0 || speed > FAN_MAX_PERCENT)
        return SendReply(&requester, FAN_APPLY_DB_ERROR, 0);
    if(speed != data.fan.fanSpeed)
        return SendReply(&requester, FAN_APPLY_DB_CHANGED, (uint16_t)speed);
    pthread_mutex_lock(&controlMutex);
    reason = StartControl(device, &requester, (uint16_t)speed);
    pthread_mutex_unlock(&controlMutex);
    // 송신 성공은 완료 ACK가 아님. 장치 ACK/끊김/시간 초과 때 PC로 응답.
    return reason == FAN_APPLY_OK ? 0 : SendReply(&requester, reason, (uint16_t)speed);
}

int RPacketFanSendSpeed(int fd, uint8_t percent)
{
    RSessionSnapshot device;
    RNetReference requester = {.fd = -1};
    uint8_t reason;

    if(fd < 0 || percent > FAN_MAX_PERCENT)
    {
        errno = EINVAL;
        return -1;
    }
    if(RSessionFindByFd(fd, &device) != 0)
        return 1;
    if(!device.authenticated || device.type != SESSION_BLUETOOTH || device.memberType != MEMBER_TYPE_STM32)
    {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&controlMutex);
    reason = StartControl(&device, &requester, percent);
    pthread_mutex_unlock(&controlMutex);
    if(reason == FAN_APPLY_OK)
        return 0;
    if(reason == FAN_APPLY_NOT_CONNECTED)
        return 1;
    errno = reason == FAN_APPLY_BUSY ? EBUSY : EIO;
    return -1;
}

void RPacketFanHandleControlAck(const RSession *session, uint8_t result)
{
    RSessionSnapshot snapshot;
    RNetReference device;

    if(session == NULL || RSessionFindByFd(session->fd, &snapshot) != 0 || RNetGetSessionReference(&snapshot, &device) != 0)
        return;
    pthread_mutex_lock(&controlMutex);
    if(control.active && control.device.fd == device.fd && control.device.token == device.token)
    {
        if(GetMonotonicMs() >= control.deadlineMs)
        {
            RNetCloseReferenced(&control.device);
            FinishControl(FAN_APPLY_TIMEOUT);
        }
        else
            FinishControl(result == RESULT_SUCCESS ? FAN_APPLY_OK : FAN_APPLY_REJECTED);
    }
    else
        RLOG_WARN("[%s] Unexpected ACK_FAN: no matching control request", session->label);
    pthread_mutex_unlock(&controlMutex);
}

void RPacketFanControlTick(uint64_t nowMs)
{
    pthread_mutex_lock(&controlMutex);
    if(control.active)
    {
        if(!RNetIsReferenceOpen(&control.device))
            FinishControl(FAN_APPLY_DISCONNECTED);
        else if(nowMs >= control.deadlineMs)
        {
            // ACK에 요청 번호가 없음. 지연 ACK를 다음 요청으로 오인하지 않도록 BT 연결 종료.
            RNetCloseReferenced(&control.device);
            FinishControl(FAN_APPLY_TIMEOUT);
        }
    }
    pthread_mutex_unlock(&controlMutex);
}

void RPacketFanResetControl(void)
{
    // 서버 시작 전/네트워크 워커 종료 후에만 호출.
    pthread_mutex_lock(&controlMutex);
    memset(&control, 0, sizeof(control));
    pthread_mutex_unlock(&controlMutex);
}

static uint64_t GetMonotonicMs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static int SendReply(const RNetReference *requester, uint8_t reason, uint16_t percent)
{
    FanApplyAckData ack = {.result = reason == FAN_APPLY_OK ? RESULT_SUCCESS : RESULT_FAIL, .reason = reason, .fan = {.fanSpeed = percent}};
    uint8_t frame[HEADER_SIZE + FAN_APPLY_ACK_DATA_SIZE];
    size_t frameLength = MakeFanApplyAckPacket(frame, sizeof(frame), &ack);

    if(requester->fd < 0)
        return 0;
    return frameLength > 0 && RNetSendReferenced(requester, frame, frameLength) == 0 ? 0 : -1;
}

// controlMutex를 잡은 상태에서 호출. 요청 등록을 송신보다 먼저 하여 즉시 ACK도 처리.
static uint8_t StartControl(const RSessionSnapshot *device, const RNetReference *requester, uint16_t percent)
{
    FanData data = {.fanSpeed = percent};
    uint8_t frame[HEADER_SIZE + FAN_DATA_SIZE];
    size_t frameLength;
    RNetReference reference;

    if(control.active)
        return FAN_APPLY_BUSY;
    if(RNetGetSessionReference(device, &reference) != 0)
        return FAN_APPLY_NOT_CONNECTED;
    frameLength = MakeFanControlPacket(frame, sizeof(frame), &data);
    if(frameLength == 0)
        return FAN_APPLY_SEND_FAILED;
    control.active = 1;
    control.device = reference;
    control.requester = *requester;
    control.percent = percent;
    control.deadlineMs = GetMonotonicMs() + FAN_CONTROL_TIMEOUT_MS;
    memcpy(control.memberId, device->memberId, sizeof(control.memberId));
    if(RNetSendReferenced(&control.device, frame, frameLength) != 0)
    {
        control.active = 0;
        return FAN_APPLY_SEND_FAILED;
    }
    RLOG_INFO("FAN apply sent: id=%s, speed=%u%%, requester_fd=%d", control.memberId, (unsigned int)percent, requester->fd);
    return FAN_APPLY_OK;
}

static void FinishControl(uint8_t reason)
{
    RLOG_INFO("FAN apply completed: id=%s, speed=%u%%, reason=%u", control.memberId, (unsigned int)control.percent, (unsigned int)reason);
    if(SendReply(&control.requester, reason, control.percent) != 0)
        RLOG_WARN("FAN apply result not delivered: requester disconnected or send failed");
    control.active = 0;
}
