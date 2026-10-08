#include "RPacketFirmware.h"
#include "IoTFirmware.h"
#include "IoTPacketCodec.h"
#include "RLog.h"
#include "RNetwork.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

// 대기 영역(128KB 섹터) 지우기: 보통 1~2초, 최악 4초
#define FIRMWARE_BEGIN_TIMEOUT_MS 10000
// 9600bps에서 268바이트 프레임 ≈ 0.3초 + 플래시 쓰기
#define FIRMWARE_CHUNK_TIMEOUT_MS 3000
// 이미지 CRC32 계산 + 대기 헤더 쓰기
#define FIRMWARE_END_TIMEOUT_MS 5000
// 응답이 없을 때 같은 요청을 다시 보내는 횟수 (장치는 이미 쓴 청크의 재전송을 성공으로 응답)
#define FIRMWARE_RETRY_COUNT 3
#define FIRMWARE_PROGRESS_PERCENT_STEP 10

typedef enum
{
    FIRMWARE_ACK_SUCCESS,
    FIRMWARE_ACK_FAIL,   // 장치가 RESULT_FAIL로 응답
    FIRMWARE_ACK_TIMEOUT // 응답 없음 또는 전송 실패
} RFirmwareAckResult;

// RPacketFirmwarePush가 기다리는 ACK 하나 (BT 워커가 채우고 깨움)
typedef struct _RFirmwareWait
{
    int fd;          // 응답을 기다리는 세션, 기다리지 않으면 -1
    uint16_t ackCmd; // 기다리는 ACK_FW_*
    uint32_t offset; // ACK_FW_CHUNK이면 기다리는 청크 오프셋
    int received;
    uint8_t result;
} RFirmwareWait;

// 전송은 한 번에 하나
static pthread_mutex_t pushMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t waitMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t waitCond = PTHREAD_COND_INITIALIZER;
static RFirmwareWait firmwareWait = {.fd = -1};

static int PushImage(int fd, const uint8_t *image, size_t size);
static RFirmwareAckResult SendRequest(int fd, const uint8_t *frame, size_t frameLength, uint16_t ackCmd, uint32_t offset, int timeoutMs);
static void ReceiveAck(const RSession *session, uint16_t ackCmd, uint32_t offset, uint8_t result);

int RPacketFirmwareBeginReceiveAck(RSession *session, const uint8_t *payload, size_t length)
{
    ResultData ack;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketFirmwareBeginReceiveAck: NULL argument");
        return -1;
    }
    if(ReadResultData(payload, length, &ack) != 0)
    {
        RLOG_WARN("[%s] Malformed ACK_FW_BEGIN payload: length=%zu", session->label, length);
        return -1;
    }
    ReceiveAck(session, ACK_FW_BEGIN, 0, ack.result);
    return 0;
}

int RPacketFirmwareChunkReceiveAck(RSession *session, const uint8_t *payload, size_t length)
{
    FirmwareChunkAckData ack;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketFirmwareChunkReceiveAck: NULL argument");
        return -1;
    }
    if(ReadFirmwareChunkAckData(payload, length, &ack) != 0)
    {
        RLOG_WARN("[%s] Malformed ACK_FW_CHUNK payload: length=%zu", session->label, length);
        return -1;
    }
    ReceiveAck(session, ACK_FW_CHUNK, ack.offset, ack.result);
    return 0;
}

int RPacketFirmwareEndReceiveAck(RSession *session, const uint8_t *payload, size_t length)
{
    ResultData ack;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketFirmwareEndReceiveAck: NULL argument");
        return -1;
    }
    if(ReadResultData(payload, length, &ack) != 0)
    {
        RLOG_WARN("[%s] Malformed ACK_FW_END payload: length=%zu", session->label, length);
        return -1;
    }
    ReceiveAck(session, ACK_FW_END, 0, ack.result);
    return 0;
}

int RPacketFirmwarePush(int fd, const uint8_t *image, size_t size)
{
    int result;

    if(fd < 0 || image == NULL)
    {
        RLOG_ERROR("RPacketFirmwarePush: invalid argument: fd=%d", fd);
        errno = EINVAL;
        return -1;
    }
    if(pthread_mutex_trylock(&pushMutex) != 0)
    {
        RLOG_WARN("Firmware push rejected: another transfer is running");
        errno = EBUSY;
        return -1;
    }
    result = PushImage(fd, image, size);
    pthread_mutex_unlock(&pushMutex);
    return result;
}

// pushMutex를 잡은 상태에서 호출
static int PushImage(int fd, const uint8_t *image, size_t size)
{
    uint8_t frame[HEADER_SIZE + FIRMWARE_CHUNK_OFFSET_SIZE + FIRMWARE_CHUNK_SIZE];
    FirmwareBeginData begin;
    FirmwareChunkData chunk;
    RFirmwareAckResult ackResult = FIRMWARE_ACK_TIMEOUT;
    size_t frameLength;
    int nextPercent = FIRMWARE_PROGRESS_PERCENT_STEP;

    if(CheckFirmwareImage(image, size, &begin.version) != 0)
    {
        RLOG_WARN("Firmware push rejected: not an STM32 application image: size=%zu", size);
        errno = EINVAL;
        return -1;
    }
    begin.size = (uint32_t)size;
    begin.crc32 = ComputeFirmwareCrc32(image, size);
    RLOG_INFO("Firmware push start: fd=%d, version=%u, size=%zu, crc32=0x%08X", fd, (unsigned int)begin.version, size, (unsigned int)begin.crc32);

    frameLength = MakeFirmwareBeginPacket(frame, sizeof(frame), &begin);
    if(frameLength == 0 || SendRequest(fd, frame, frameLength, ACK_FW_BEGIN, 0, FIRMWARE_BEGIN_TIMEOUT_MS) != FIRMWARE_ACK_SUCCESS)
    {
        RLOG_WARN("Firmware push failed: REQ_FW_BEGIN not accepted: fd=%d", fd);
        errno = EIO;
        return -1;
    }

    for(size_t offset = 0; offset < size; offset += chunk.length)
    {
        chunk.offset = (uint32_t)offset;
        chunk.length = (uint16_t)(size - offset < FIRMWARE_CHUNK_SIZE ? size - offset : FIRMWARE_CHUNK_SIZE);
        memcpy(chunk.data, image + offset, chunk.length);
        frameLength = MakeFirmwareChunkPacket(frame, sizeof(frame), &chunk);
        if(frameLength == 0)
        {
            RLOG_ERROR("REQ_FW_CHUNK frame build failed: offset=%zu", offset);
            errno = EIO;
            return -1;
        }
        // 실패 응답(쓰기 오류, 순서 어긋남)은 다시 보내도 같으므로 응답이 없을 때만 재시도
        ackResult = FIRMWARE_ACK_TIMEOUT;
        for(int tryCount = 0; tryCount < FIRMWARE_RETRY_COUNT && ackResult == FIRMWARE_ACK_TIMEOUT; ++tryCount)
        {
            ackResult = SendRequest(fd, frame, frameLength, ACK_FW_CHUNK, chunk.offset, FIRMWARE_CHUNK_TIMEOUT_MS);
        }
        if(ackResult != FIRMWARE_ACK_SUCCESS)
        {
            RLOG_WARN("Firmware push failed: offset=%zu/%zu, reason=%s", offset, size, ackResult == FIRMWARE_ACK_FAIL ? "rejected by device" : "no response");
            errno = ackResult == FIRMWARE_ACK_FAIL ? EIO : ETIMEDOUT;
            return -1;
        }
        if((offset + chunk.length) * 100 >= size * (size_t)nextPercent)
        {
            RLOG_INFO("Firmware push: %zu/%zu bytes (%zu%%)", offset + chunk.length, size, (offset + chunk.length) * 100 / size);
            nextPercent += FIRMWARE_PROGRESS_PERCENT_STEP;
        }
    }

    frameLength = MakeFirmwareEndPacket(frame, sizeof(frame));
    ackResult = frameLength > 0 ? SendRequest(fd, frame, frameLength, ACK_FW_END, 0, FIRMWARE_END_TIMEOUT_MS) : FIRMWARE_ACK_TIMEOUT;
    if(ackResult != FIRMWARE_ACK_SUCCESS)
    {
        RLOG_WARN("Firmware push failed: REQ_FW_END %s: fd=%d", ackResult == FIRMWARE_ACK_FAIL ? "rejected (CRC32 or image check)" : "no response", fd);
        errno = ackResult == FIRMWARE_ACK_FAIL ? EIO : ETIMEDOUT;
        return -1;
    }
    RLOG_INFO("Firmware push done: fd=%d, version=%u (device reboots to install)", fd, (unsigned int)begin.version);
    return 0;
}

// 기다릴 ACK를 먼저 등록한 뒤 보냄 (응답이 보내기 직후 와도 놓치지 않도록)
static RFirmwareAckResult SendRequest(int fd, const uint8_t *frame, size_t frameLength, uint16_t ackCmd, uint32_t offset, int timeoutMs)
{
    struct timespec deadline;
    RFirmwareAckResult result = FIRMWARE_ACK_TIMEOUT;
    int waitResult = 0;

    pthread_mutex_lock(&waitMutex);
    firmwareWait.fd = fd;
    firmwareWait.ackCmd = ackCmd;
    firmwareWait.offset = offset;
    firmwareWait.received = 0;
    pthread_mutex_unlock(&waitMutex);

    if(RNetSend(fd, frame, frameLength) != 0)
    {
        RLOG_WARN("Firmware request send failed: fd=%d, cmd=0x%04X", fd, (unsigned int)ACK_TO_REQ(ackCmd));
    }
    else
    {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeoutMs / 1000;
        deadline.tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
        if(deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&waitMutex);
        while(!firmwareWait.received && waitResult != ETIMEDOUT)
        {
            waitResult = pthread_cond_timedwait(&waitCond, &waitMutex, &deadline);
        }
        if(firmwareWait.received)
        {
            result = firmwareWait.result == RESULT_SUCCESS ? FIRMWARE_ACK_SUCCESS : FIRMWARE_ACK_FAIL;
        }
        pthread_mutex_unlock(&waitMutex);
    }

    pthread_mutex_lock(&waitMutex);
    firmwareWait.fd = -1;
    pthread_mutex_unlock(&waitMutex);
    return result;
}

// BT 워커에서 호출. 시간이 지나 재전송한 뒤 늦게 온 ACK는 오프셋이 다르면 버림
static void ReceiveAck(const RSession *session, uint16_t ackCmd, uint32_t offset, uint8_t result)
{
    int matched;

    pthread_mutex_lock(&waitMutex);
    matched = firmwareWait.fd == session->fd && firmwareWait.ackCmd == ackCmd && !firmwareWait.received && (ackCmd != ACK_FW_CHUNK || firmwareWait.offset == offset);
    if(matched)
    {
        firmwareWait.received = 1;
        firmwareWait.result = result;
        pthread_cond_broadcast(&waitCond);
    }
    pthread_mutex_unlock(&waitMutex);
    if(!matched)
    {
        RLOG_WARN("[%s] Unexpected firmware ACK: cmd=0x%04X, offset=%u, result=%u", session->label, (unsigned int)ackCmd, (unsigned int)offset, (unsigned int)result);
    }
}
