#include "RSession.h"
#include "IoTPacketStream.h"
#include "RLog.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SESSION_LABEL_SIZE 64
#define SESSION_RX_BUFFER_SIZE (PACKET_FRAME_SIZE * 2)
#define SESSION_TX_BUFFER_SIZE (PACKET_FRAME_SIZE * 4)
#define SESSION_EPOLL_EVENTS 32
// epoll_wait 최대 대기. 이 주기로 타임아웃과 다른 스레드의 요청을 확인
#define SESSION_TICK_MS 200
#define TLS_HANDSHAKE_TIMEOUT_MS 5000
// 프레임 첫 바이트를 받은 뒤 프레임 전체가 도착해야 하는 시간
#define FRAME_TIMEOUT_MS 5000
// 송신 큐가 이 시간 동안 줄지 않으면 상대가 받지 않는 것으로 보고 끊음
#define SEND_STALL_TIMEOUT_MS 5000
// 닫기 전에 남은 송신(예: 실패 ACK)을 보내는 최대 시간
#define CLOSE_FLUSH_TIMEOUT_MS 2000
#define BT_CONNECT_TIMEOUT_MS 5000

typedef enum
{
    SESSION_STATE_HANDSHAKE,
    SESSION_STATE_OPEN,
    SESSION_STATE_CLOSING,
    SESSION_STATE_CLOSED
} RSessionState;

// fd 하나 = 세션 하나. 참조(I/O 스레드 등록 1 + 처리 중 작업 + 진행 중인 RSessionSend)가 0이 되면 fd를 닫고 슬롯 반환.
struct _RSession
{
    // tableMutex 보호
    int index;
    int inUse;
    int refCount;
    int registered;      // epoll에 등록되어 I/O 스레드가 관리 중
    int open;            // 송신 가능 (핸드셰이크 완료 ~ 닫기 시작 전)
    int jobDone;         // 워커가 프레임 처리를 마침
    int closeRequested;  // 송신 큐를 비운 뒤 닫기
    int stopRequested;   // 바로 닫기
    char memberId[MEM_ID_SIZE + 1];
    int authenticated;

    // 등록 후 변하지 않음
    RSessionType type;
    int fd;
    SSL *tls;
    char address[SESSION_ADDRESS_SIZE];
    char label[SESSION_LABEL_SIZE];

    // I/O 스레드 전용
    RSessionState state;
    uint32_t interest;
    int processing;      // 워커가 frame*을 처리 중. 끝날 때까지 수신을 멈춤
    int tlsWantWrite;    // SSL_accept/SSL_read가 쓰기 가능을 기다림
    uint64_t deadlineMs; // 핸드셰이크 또는 닫기 마감
    uint64_t frameStartMs;
    uint8_t rx[SESSION_RX_BUFFER_SIZE];
    size_t rxLength;

    // processing 동안 워커 전용
    uint16_t frameCmd;
    uint16_t frameLength;
    uint8_t framePayload[MAX_PAYLOAD_SIZE];

    // txMutex 보호. 아무 스레드나 넣고 I/O 스레드가 보냄
    pthread_mutex_t txMutex;
    uint8_t tx[SESSION_TX_BUFFER_SIZE];
    size_t txLength;
    size_t txRetryLength; // SSL_write가 WANT_*를 돌려주면 같은 길이로 다시 호출해야 함
    uint64_t txProgressMs;
};

static RSession sessions[MAX_SESSION];
static int sessionCount;
static int cleanupCount;
static int sessionClosing;
static int sessionInitialized;
static int ioStopRequested;
// 슬롯/참조/인증 상태 보호. 잠금 순서: tableMutex → txMutex
static pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sessionIdleCond = PTHREAD_COND_INITIALIZER;
// BT 연결/교체 직렬화
static pthread_mutex_t btConnectMutex = PTHREAD_MUTEX_INITIALIZER;
static RSessionHandler sessionHandler;
static RThreadPool *sessionPool;
static int epollFd = -1;
static int wakeFd = -1;
static pthread_t ioThread;

static uint64_t GetMonotonicMs(void);
static void WakeSessionLoop(void);
static RSession *ClaimSession(RSessionType type, int fd);
static int RegisterSession(RSession *session);
static void ReleaseSession(RSession *session);
static void *RunSessionLoop(void *arg);
static void ServiceSession(RSession *session, uint32_t events);
static void CloseSession(RSession *session);
static void BeginClosing(RSession *session, uint64_t now);
static int ContinueHandshake(RSession *session);
static int ReceiveFrames(RSession *session, uint64_t now);
static int ReadSessionData(RSession *session);
static int ParseFrame(RSession *session, uint64_t now);
static int SubmitFrame(RSession *session);
static void ProcessFrameJob(void *argument);
static int FlushSession(RSession *session, uint64_t now);
static int IsSessionTimedOut(RSession *session, uint64_t now);
static int HasPendingSend(RSession *session);
static void UpdateInterest(RSession *session);

int RSessionInit(RSessionHandler handler, RThreadPool *pool)
{
    struct epoll_event wakeEvent = {.events = EPOLLIN, .data.ptr = NULL};
    int createResult;

    if(handler == NULL || pool == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    sessionHandler = handler;
    sessionPool = pool;
    sessionClosing = 0;
    ioStopRequested = 0;
    if(!sessionInitialized)
    {
        for(int index = 0; index < MAX_SESSION; ++index)
        {
            sessions[index].index = index;
            sessions[index].fd = -1;
            if(pthread_mutex_init(&sessions[index].txMutex, NULL) != 0)
            {
                pthread_mutex_unlock(&tableMutex);
                return -1;
            }
        }
        sessionInitialized = 1;
    }
    pthread_mutex_unlock(&tableMutex);

    epollFd = epoll_create1(EPOLL_CLOEXEC);
    wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if(epollFd < 0 || wakeFd < 0 || epoll_ctl(epollFd, EPOLL_CTL_ADD, wakeFd, &wakeEvent) != 0)
    {
        RLOG_ERROR("Session epoll initialization failed: %s", strerror(errno));
        return -1;
    }
    createResult = pthread_create(&ioThread, NULL, RunSessionLoop, NULL);
    if(createResult != 0)
    {
        RLOG_ERROR("pthread_create(session I/O): %s", strerror(createResult));
        errno = createResult;
        return -1;
    }
    return 0;
}

void RSessionCloseAll(void)
{
    if(epollFd < 0)
    {
        return;
    }
    pthread_mutex_lock(&tableMutex);
    sessionClosing = 1;
    pthread_mutex_unlock(&tableMutex);
    WakeSessionLoop();

    // I/O 스레드가 모든 세션을 닫고, 처리 중인 작업이 참조를 놓을 때까지 대기
    pthread_mutex_lock(&tableMutex);
    while(sessionCount > 0 || cleanupCount > 0)
    {
        pthread_cond_wait(&sessionIdleCond, &tableMutex);
    }
    ioStopRequested = 1;
    pthread_mutex_unlock(&tableMutex);
    WakeSessionLoop();
    pthread_join(ioThread, NULL);

    close(epollFd);
    close(wakeFd);
    epollFd = -1;
    wakeFd = -1;
}

int RSessionOpenTcp(int fd, SSL *tls, const struct sockaddr_in *address)
{
    RSession *session = NULL;

    if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0)
    {
        // 송신 큐의 앞부분만 보내고 나머지를 이어 붙이므로 부분 쓰기와 버퍼 이동을 허용
        SSL_set_mode(tls, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        pthread_mutex_lock(&tableMutex);
        session = sessionClosing ? NULL : ClaimSession(SESSION_TCP, fd);
        if(session != NULL)
        {
            session->tls = tls;
            inet_ntop(AF_INET, &address->sin_addr, session->address, sizeof(session->address));
            snprintf(session->label, sizeof(session->label), "%s", session->address);
            session->state = SESSION_STATE_HANDSHAKE;
            session->deadlineMs = GetMonotonicMs() + TLS_HANDSHAKE_TIMEOUT_MS;
            session->interest = EPOLLIN;
        }
        pthread_mutex_unlock(&tableMutex);
    }
    if(session == NULL)
    {
        RLOG_WARN("TCP client rejected: session limit reached or server stopping");
        SSL_free(tls);
        close(fd);
        return -1;
    }
    return RegisterSession(session);
}

int RSessionOpenBt(const char *memberId, const char *mac)
{
    RSession *previous = NULL;
    RSession *session = NULL;
    size_t memberIdLength;
    uint8_t rfcommChannel;
    int claimError;
    int fd;

    if(memberId == NULL || mac == NULL || (memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1)) == 0 || memberIdLength > MEM_ID_SIZE || strnlen(mac, SESSION_ADDRESS_SIZE) != BLUETOOTH_MAC_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&btConnectMutex);
    pthread_mutex_lock(&tableMutex);
    if(sessionClosing)
    {
        pthread_mutex_unlock(&tableMutex);
        pthread_mutex_unlock(&btConnectMutex);
        errno = ECANCELED;
        return -1;
    }
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        RSession *candidate = &sessions[index];

        if(!candidate->inUse || candidate->type != SESSION_BLUETOOTH || !candidate->open)
        {
            continue;
        }
        if(strcmp(candidate->memberId, memberId) == 0)
        {
            if(strcasecmp(candidate->address, mac) == 0)
            {
                fd = candidate->fd;
                pthread_mutex_unlock(&tableMutex);
                pthread_mutex_unlock(&btConnectMutex);
                RLOG_INFO("Bluetooth already connected: id=%s, mac=%s, fd=%d", memberId, mac, fd);
                return fd;
            }
            previous = candidate;
        }
        else if(strcasecmp(candidate->address, mac) == 0)
        {
            pthread_mutex_unlock(&tableMutex);
            pthread_mutex_unlock(&btConnectMutex);
            RLOG_WARN("Bluetooth MAC already connected to another member: id=%s, mac=%s", memberId, mac);
            errno = EADDRINUSE;
            return -1;
        }
    }
    // 등록 MAC이 바뀜: 이전 세션은 닫고 새로 연결
    if(previous != NULL)
    {
        previous->stopRequested = 1;
        previous->open = 0;
    }
    pthread_mutex_unlock(&tableMutex);
    if(previous != NULL)
    {
        WakeSessionLoop();
    }

    fd = ConnectBluetoothDevice(mac, BT_CONNECT_TIMEOUT_MS, &rfcommChannel);
    if(fd < 0)
    {
        int connectError = errno;

        RLOG_WARN("[BT] HC-05 connection failed: id=%s, mac=%s: %s", memberId, mac, strerror(connectError));
        pthread_mutex_unlock(&btConnectMutex);
        errno = connectError;
        return -1;
    }

    claimError = ECANCELED;
    if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0)
    {
        pthread_mutex_lock(&tableMutex);
        session = sessionClosing ? NULL : ClaimSession(SESSION_BLUETOOTH, fd);
        claimError = sessionClosing ? ECANCELED : ENOSPC;
        if(session != NULL)
        {
            session->authenticated = 1;
            memcpy(session->memberId, memberId, memberIdLength + 1);
            memcpy(session->address, mac, BLUETOOTH_MAC_SIZE);
            session->address[BLUETOOTH_MAC_SIZE] = '\0';
            snprintf(session->label, sizeof(session->label), "BT id=%s mac=%s", session->memberId, session->address);
            session->state = SESSION_STATE_OPEN;
            session->open = 1;
            session->interest = EPOLLIN;
        }
        pthread_mutex_unlock(&tableMutex);
    }
    if(session == NULL)
    {
        RLOG_WARN("[BT] Session not available: id=%s", memberId);
        DisconnectBluetoothDevice(fd);
        pthread_mutex_unlock(&btConnectMutex);
        errno = claimError;
        return -1;
    }
    if(RegisterSession(session) != 0)
    {
        pthread_mutex_unlock(&btConnectMutex);
        return -1;
    }
    pthread_mutex_unlock(&btConnectMutex);
    RLOG_INFO("[BT] HC-05 connected: id=%s, mac=%s, fd=%d, channel=%u", memberId, mac, fd, (unsigned int)rfcommChannel);
    return fd;
}

int RSessionSend(int fd, const uint8_t *frame, size_t frameLength)
{
    RSession *session = NULL;
    int result;

    if(fd < 0 || frame == NULL || frameLength < HEADER_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    // 큐에 넣는 동안 세션이 닫혀도 fd가 재사용되지 않게 참조를 잡음
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        if(sessions[index].inUse && sessions[index].open && sessions[index].fd == fd)
        {
            session = &sessions[index];
            session->refCount++;
            break;
        }
    }
    pthread_mutex_unlock(&tableMutex);
    if(session == NULL)
    {
        return 1;
    }

    result = RSessionSendFrame(session, frame, frameLength);
    if(result != 0)
    {
        RLOG_WARN("[%s] Packet send failed: fd=%d: %s", session->label, fd, strerror(errno));
    }
    ReleaseSession(session);
    return result;
}

int RSessionFindBtFd(const char *memberId)
{
    int fd = -1;

    if(memberId == NULL)
    {
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        const RSession *session = &sessions[index];

        if(session->inUse && session->open && session->type == SESSION_BLUETOOTH && strcmp(session->memberId, memberId) == 0)
        {
            fd = session->fd;
            break;
        }
    }
    pthread_mutex_unlock(&tableMutex);
    return fd;
}

size_t RSessionGetSnapshots(RSessionSnapshot *snapshots)
{
    size_t snapshotCount = 0;

    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        const RSession *session = &sessions[index];
        RSessionSnapshot *snapshot;

        if(!session->inUse || !session->registered)
        {
            continue;
        }
        snapshot = &snapshots[snapshotCount++];
        snapshot->index = session->index;
        snapshot->type = session->type;
        snapshot->fd = session->fd;
        snapshot->connected = session->open;
        snapshot->authenticated = session->authenticated;
        memcpy(snapshot->address, session->address, sizeof(snapshot->address));
        memcpy(snapshot->memberId, session->memberId, sizeof(snapshot->memberId));
    }
    pthread_mutex_unlock(&tableMutex);
    return snapshotCount;
}

int RSessionSendFrame(RSession *session, const void *frame, size_t frameLength)
{
    int open;

    if(frame == NULL || frameLength == 0)
    {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    open = session->open;
    pthread_mutex_unlock(&tableMutex);
    if(!open)
    {
        errno = ENOTCONN;
        return -1;
    }

    pthread_mutex_lock(&session->txMutex);
    if(session->txLength + frameLength > sizeof(session->tx))
    {
        pthread_mutex_unlock(&session->txMutex);
        errno = ENOBUFS;
        return -1;
    }
    if(session->txLength == 0)
    {
        session->txProgressMs = GetMonotonicMs();
    }
    memcpy(session->tx + session->txLength, frame, frameLength);
    session->txLength += frameLength;
    pthread_mutex_unlock(&session->txMutex);
    WakeSessionLoop();
    return 0;
}

RSessionType RSessionGetType(const RSession *session)
{
    return session->type;
}

int RSessionGetFd(const RSession *session)
{
    return session->fd;
}

const char *RSessionGetLabel(const RSession *session)
{
    return session->label;
}

int RSessionIsAuthenticated(RSession *session)
{
    int authenticated;

    pthread_mutex_lock(&tableMutex);
    authenticated = session->authenticated;
    pthread_mutex_unlock(&tableMutex);
    return authenticated;
}

int RSessionGetMemberId(RSession *session, char memberId[MEM_ID_SIZE + 1])
{
    int result = -1;

    pthread_mutex_lock(&tableMutex);
    if(session->authenticated && session->memberId[0] != '\0')
    {
        memcpy(memberId, session->memberId, MEM_ID_SIZE + 1);
        result = 0;
    }
    pthread_mutex_unlock(&tableMutex);
    return result;
}

void RSessionLogin(RSession *session, const char *memberId, size_t memberIdLength)
{
    if(memberIdLength > MEM_ID_SIZE)
    {
        memberIdLength = MEM_ID_SIZE;
    }
    pthread_mutex_lock(&tableMutex);
    memcpy(session->memberId, memberId, memberIdLength);
    session->memberId[memberIdLength] = '\0';
    session->authenticated = 1;
    pthread_mutex_unlock(&tableMutex);
}

void RSessionLogout(RSession *session)
{
    pthread_mutex_lock(&tableMutex);
    session->memberId[0] = '\0';
    session->authenticated = 0;
    pthread_mutex_unlock(&tableMutex);
}

static uint64_t GetMonotonicMs(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void WakeSessionLoop(void)
{
    uint64_t signal = 1;
    ssize_t writeResult;

    do
    {
        writeResult = write(wakeFd, &signal, sizeof(signal));
    }
    while(writeResult < 0 && errno == EINTR);
}

// tableMutex를 잡은 상태에서 호출. 빈 슬롯을 초기화해 반환 (I/O 스레드 참조 1개), 없으면 NULL.
static RSession *ClaimSession(RSessionType type, int fd)
{
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        RSession *session = &sessions[index];

        if(session->inUse)
        {
            continue;
        }
        session->inUse = 1;
        session->refCount = 1;
        session->registered = 0;
        session->open = 0;
        session->jobDone = 0;
        session->closeRequested = 0;
        session->stopRequested = 0;
        session->authenticated = 0;
        memset(session->memberId, 0, sizeof(session->memberId));
        session->type = type;
        session->fd = fd;
        session->tls = NULL;
        memset(session->address, 0, sizeof(session->address));
        memset(session->label, 0, sizeof(session->label));
        session->state = SESSION_STATE_OPEN;
        session->interest = 0;
        session->processing = 0;
        session->tlsWantWrite = 0;
        session->deadlineMs = 0;
        session->frameStartMs = 0;
        session->rxLength = 0;
        pthread_mutex_lock(&session->txMutex);
        session->txLength = 0;
        session->txRetryLength = 0;
        pthread_mutex_unlock(&session->txMutex);
        sessionCount++;
        return session;
    }
    return NULL;
}

// epoll에 등록하고 I/O 스레드에 넘김. 실패하면 세션을 해제(fd 닫음)하고 -1
static int RegisterSession(RSession *session)
{
    struct epoll_event event = {.events = session->interest, .data.ptr = session};

    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, session->fd, &event) != 0)
    {
        int registerError = errno;

        RLOG_ERROR("[%s] epoll_ctl(ADD): %s", session->label, strerror(registerError));
        pthread_mutex_lock(&tableMutex);
        session->open = 0;
        pthread_mutex_unlock(&tableMutex);
        ReleaseSession(session);
        errno = registerError;
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    session->registered = 1;
    pthread_mutex_unlock(&tableMutex);
    WakeSessionLoop();
    return 0;
}

// 참조 하나 반환. 마지막 참조가 fd/TLS를 닫고 슬롯을 비움.
static void ReleaseSession(RSession *session)
{
    RSessionType type = session->type;
    int fd = -1;
    SSL *tls = NULL;
    int remainingSessions = 0;
    char label[SESSION_LABEL_SIZE];

    pthread_mutex_lock(&tableMutex);
    if(--session->refCount == 0)
    {
        fd = session->fd;
        tls = session->tls;
        memcpy(label, session->label, sizeof(label));
        session->fd = -1;
        session->tls = NULL;
        session->authenticated = 0;
        memset(session->memberId, 0, sizeof(session->memberId));
        session->inUse = 0;
        sessionCount--;
        remainingSessions = sessionCount;
        ++cleanupCount;
    }
    pthread_mutex_unlock(&tableMutex);
    if(fd < 0)
    {
        return;
    }

    if(type == SESSION_TCP)
    {
        SSL_free(tls);
        close(fd);
    }
    else
    {
        DisconnectBluetoothDevice(fd);
    }
    RLOG_INFO("Session closed: %s, fd=%d, sessions=%d", label, fd, remainingSessions);

    pthread_mutex_lock(&tableMutex);
    --cleanupCount;
    pthread_cond_broadcast(&sessionIdleCond);
    pthread_mutex_unlock(&tableMutex);
}

/* ============================================================================
   I/O 스레드: 모든 세션의 수신/송신/핸드셰이크/타임아웃
   ============================================================================ */

static void *RunSessionLoop(void *arg)
{
    struct epoll_event events[SESSION_EPOLL_EVENTS];

    (void)arg;
    while(1)
    {
        RSession *registered[MAX_SESSION];
        size_t registeredCount = 0;
        int eventCount;
        int stop;

        pthread_mutex_lock(&tableMutex);
        stop = ioStopRequested;
        pthread_mutex_unlock(&tableMutex);
        if(stop)
        {
            break;
        }

        eventCount = epoll_wait(epollFd, events, SESSION_EPOLL_EVENTS, SESSION_TICK_MS);
        if(eventCount < 0 && errno != EINTR)
        {
            RLOG_ERROR("epoll_wait: %s", strerror(errno));
        }
        for(int index = 0; index < eventCount; ++index)
        {
            if(events[index].data.ptr == NULL)
            {
                uint64_t signal;

                while(read(wakeFd, &signal, sizeof(signal)) > 0)
                {
                }
                continue;
            }
            ServiceSession((RSession *)events[index].data.ptr, events[index].events);
        }

        // 다른 스레드의 요청(송신, 작업 완료, 닫기)과 타임아웃은 매 주기 모든 세션에서 확인
        pthread_mutex_lock(&tableMutex);
        for(int index = 0; index < MAX_SESSION; ++index)
        {
            if(sessions[index].inUse && sessions[index].registered)
            {
                registered[registeredCount++] = &sessions[index];
            }
        }
        pthread_mutex_unlock(&tableMutex);
        for(size_t index = 0; index < registeredCount; ++index)
        {
            ServiceSession(registered[index], 0);
        }
    }
    return NULL;
}

static void ServiceSession(RSession *session, uint32_t events)
{
    uint64_t now = GetMonotonicMs();
    int stop;
    int closeRequested;
    int jobDone;

    pthread_mutex_lock(&tableMutex);
    if(!session->inUse || !session->registered)
    {
        pthread_mutex_unlock(&tableMutex);
        return;
    }
    stop = session->stopRequested || sessionClosing;
    closeRequested = session->closeRequested;
    jobDone = session->jobDone;
    session->closeRequested = 0;
    session->jobDone = 0;
    pthread_mutex_unlock(&tableMutex);

    // 수신을 멈춘 상태에서는 끊김 이벤트가 계속 오므로 바로 닫음
    if(stop || (events & EPOLLERR) || ((events & EPOLLHUP) && (session->processing || session->state == SESSION_STATE_CLOSING)))
    {
        CloseSession(session);
        return;
    }
    if(jobDone)
    {
        session->processing = 0;
        session->frameStartMs = session->rxLength > 0 ? now : 0;
    }
    if(closeRequested && session->state != SESSION_STATE_CLOSING)
    {
        BeginClosing(session, now);
    }
    if(session->state == SESSION_STATE_HANDSHAKE && ContinueHandshake(session) != 0)
    {
        CloseSession(session);
        return;
    }
    if(session->state == SESSION_STATE_OPEN && ReceiveFrames(session, now) != 0)
    {
        CloseSession(session);
        return;
    }
    if(FlushSession(session, now) != 0 || IsSessionTimedOut(session, now))
    {
        CloseSession(session);
        return;
    }
    if(session->state == SESSION_STATE_CLOSING && !HasPendingSend(session))
    {
        CloseSession(session);
        return;
    }
    UpdateInterest(session);
}

// epoll에서 빼고 I/O 스레드 참조를 놓음. 처리 중인 작업이 있으면 그 작업이 끝날 때 fd가 닫힘.
static void CloseSession(RSession *session)
{
    epoll_ctl(epollFd, EPOLL_CTL_DEL, session->fd, NULL);
    session->state = SESSION_STATE_CLOSED;
    pthread_mutex_lock(&tableMutex);
    session->registered = 0;
    session->open = 0;
    pthread_mutex_unlock(&tableMutex);
    pthread_mutex_lock(&session->txMutex);
    session->txLength = 0;
    session->txRetryLength = 0;
    pthread_mutex_unlock(&session->txMutex);
    ReleaseSession(session);
}

// 더 받지도 넣지도 않고, 이미 넣은 송신만 보낸 뒤 닫음
static void BeginClosing(RSession *session, uint64_t now)
{
    session->state = SESSION_STATE_CLOSING;
    session->deadlineMs = now + CLOSE_FLUSH_TIMEOUT_MS;
    pthread_mutex_lock(&tableMutex);
    session->open = 0;
    pthread_mutex_unlock(&tableMutex);
}

static int ContinueHandshake(RSession *session)
{
    int result;
    int tlsError;
    int count;

    session->tlsWantWrite = 0;
    ERR_clear_error();
    result = SSL_accept(session->tls);
    if(result == 1)
    {
        session->state = SESSION_STATE_OPEN;
        pthread_mutex_lock(&tableMutex);
        session->open = 1;
        count = sessionCount;
        pthread_mutex_unlock(&tableMutex);
        RLOG_INFO("Client connected: ip=%s, fd=%d, sessions=%d", session->address, session->fd, count);
        return 0;
    }
    tlsError = SSL_get_error(session->tls, result);
    if(tlsError == SSL_ERROR_WANT_READ)
    {
        return 0;
    }
    if(tlsError == SSL_ERROR_WANT_WRITE)
    {
        session->tlsWantWrite = 1;
        return 0;
    }
    ERR_clear_error();
    RLOG_WARN("[%s] TLS handshake failed", session->label);
    return -1;
}

// 처리 중인 프레임이 없으면 버퍼의 프레임을 넘기고, 모자라면 소켓에서 더 읽음
static int ReceiveFrames(RSession *session, uint64_t now)
{
    session->tlsWantWrite = 0;
    while(!session->processing)
    {
        int parseResult = ParseFrame(session, now);
        int readResult;

        if(parseResult != 0)
        {
            return parseResult < 0 ? -1 : 0;
        }
        readResult = ReadSessionData(session);
        if(readResult <= 0)
        {
            return readResult;
        }
    }
    return 0;
}

// >0: 읽은 바이트 수, 0: 지금 읽을 것 없음, -1: 끊김/오류
static int ReadSessionData(RSession *session)
{
    size_t space = sizeof(session->rx) - session->rxLength;

    if(space == 0)
    {
        return 0;
    }
    if(session->type == SESSION_TCP)
    {
        int result;
        int tlsError;

        ERR_clear_error();
        result = SSL_read(session->tls, session->rx + session->rxLength, (int)space);
        if(result > 0)
        {
            session->rxLength += (size_t)result;
            return result;
        }
        tlsError = SSL_get_error(session->tls, result);
        if(tlsError == SSL_ERROR_WANT_READ)
        {
            return 0;
        }
        if(tlsError == SSL_ERROR_WANT_WRITE)
        {
            session->tlsWantWrite = 1;
            return 0;
        }
        ERR_clear_error();
        return -1;
    }
    else
    {
        ssize_t result = recv(session->fd, session->rx + session->rxLength, space, MSG_DONTWAIT);

        if(result > 0)
        {
            session->rxLength += (size_t)result;
            return (int)result;
        }
        if(result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            return 0;
        }
        return -1;
    }
}

// 1: 프레임 하나를 워커에 넘김, 0: 데이터 부족, -1: 깨진 프레임 (연결을 끊음)
static int ParseFrame(RSession *session, uint64_t now)
{
    HeaderData header;
    size_t frameSize;

    if(session->rxLength > 0 && session->frameStartMs == 0)
    {
        session->frameStartMs = now;
    }
    if(session->rxLength < HEADER_SIZE)
    {
        return 0;
    }
    DecodePacketHeader(session->rx, &header);
    if(header.length > MAX_PAYLOAD_SIZE)
    {
        RLOG_WARN("[%s] Invalid data length: cmd=0x%04X, length=%u", session->label, (unsigned int)header.cmd, (unsigned int)header.length);
        return -1;
    }
    frameSize = HEADER_SIZE + header.length;
    if(session->rxLength < frameSize)
    {
        return 0;
    }
    if(CheckPacketCrc(session->rx, &header, session->rx + HEADER_SIZE) != 0)
    {
        RLOG_WARN("[%s] CRC mismatch: cmd=0x%04X", session->label, (unsigned int)header.cmd);
        return -1;
    }

    session->frameCmd = header.cmd;
    session->frameLength = header.length;
    memcpy(session->framePayload, session->rx + HEADER_SIZE, header.length);
    session->rxLength -= frameSize;
    memmove(session->rx, session->rx + frameSize, session->rxLength);
    // 로그인 비밀번호 등이 수신 버퍼에 남지 않게 지움
    explicit_bzero(session->rx + session->rxLength, frameSize);
    session->frameStartMs = 0;
    session->processing = 1;
    return SubmitFrame(session) == 0 ? 1 : -1;
}

static int SubmitFrame(RSession *session)
{
    pthread_mutex_lock(&tableMutex);
    session->refCount++;
    pthread_mutex_unlock(&tableMutex);
    if(RThreadPoolSubmit(sessionPool, ProcessFrameJob, session) != 0)
    {
        RLOG_WARN("[%s] Worker queue full; closing session", session->label);
        explicit_bzero(session->framePayload, session->frameLength);
        ReleaseSession(session);
        return -1;
    }
    return 0;
}

// 워커 스레드: 프레임 하나 처리 후 I/O 스레드에 완료를 알림
static void ProcessFrameJob(void *argument)
{
    RSession *session = (RSession *)argument;
    int result = sessionHandler(session, session->frameCmd, session->framePayload, session->frameLength);

    explicit_bzero(session->framePayload, session->frameLength);
    pthread_mutex_lock(&tableMutex);
    session->jobDone = 1;
    if(result != 0)
    {
        session->closeRequested = 1;
    }
    pthread_mutex_unlock(&tableMutex);
    WakeSessionLoop();
    ReleaseSession(session);
}

// 송신 큐를 소켓이 받는 만큼 보냄. -1: 연결 오류
static int FlushSession(RSession *session, uint64_t now)
{
    int result = 0;

    if(session->state == SESSION_STATE_HANDSHAKE)
    {
        return 0;
    }
    pthread_mutex_lock(&session->txMutex);
    while(session->txLength > 0)
    {
        size_t length = session->txRetryLength != 0 ? session->txRetryLength : session->txLength;
        size_t sentLength;

        if(session->type == SESSION_TCP)
        {
            int writeResult;
            int tlsError;

            ERR_clear_error();
            writeResult = SSL_write(session->tls, session->tx, (int)length);
            if(writeResult <= 0)
            {
                tlsError = SSL_get_error(session->tls, writeResult);
                if(tlsError == SSL_ERROR_WANT_WRITE || tlsError == SSL_ERROR_WANT_READ)
                {
                    session->txRetryLength = length;
                    break;
                }
                ERR_clear_error();
                result = -1;
                break;
            }
            sentLength = (size_t)writeResult;
        }
        else
        {
            ssize_t sendResult = send(session->fd, session->tx, length, MSG_DONTWAIT | MSG_NOSIGNAL);

            if(sendResult < 0)
            {
                if(errno == EINTR)
                {
                    continue;
                }
                if(errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    result = -1;
                }
                break;
            }
            sentLength = (size_t)sendResult;
        }
        session->txLength -= sentLength;
        memmove(session->tx, session->tx + sentLength, session->txLength);
        session->txRetryLength = 0;
        session->txProgressMs = now;
    }
    pthread_mutex_unlock(&session->txMutex);
    return result;
}

static int IsSessionTimedOut(RSession *session, uint64_t now)
{
    if(session->state == SESSION_STATE_HANDSHAKE && now >= session->deadlineMs)
    {
        RLOG_WARN("[%s] TLS handshake timeout", session->label);
        return 1;
    }
    if(session->state == SESSION_STATE_CLOSING && now >= session->deadlineMs)
    {
        return 1;
    }
    if(session->state == SESSION_STATE_OPEN && !session->processing && session->frameStartMs != 0 && now - session->frameStartMs >= FRAME_TIMEOUT_MS)
    {
        RLOG_WARN("[%s] Frame timeout", session->label);
        return 1;
    }
    if(HasPendingSend(session))
    {
        uint64_t progressMs;

        pthread_mutex_lock(&session->txMutex);
        progressMs = session->txProgressMs;
        pthread_mutex_unlock(&session->txMutex);
        if(now - progressMs >= SEND_STALL_TIMEOUT_MS)
        {
            RLOG_WARN("[%s] Send stalled", session->label);
            return 1;
        }
    }
    return 0;
}

static int HasPendingSend(RSession *session)
{
    int pending;

    pthread_mutex_lock(&session->txMutex);
    pending = session->txLength > 0;
    pthread_mutex_unlock(&session->txMutex);
    return pending;
}

// 상태에 맞게 epoll 관심 이벤트 갱신 (level-triggered)
static void UpdateInterest(RSession *session)
{
    uint32_t interest = 0;

    if(session->state == SESSION_STATE_HANDSHAKE)
    {
        interest = session->tlsWantWrite ? EPOLLOUT : EPOLLIN;
    }
    else if(session->state == SESSION_STATE_OPEN && !session->processing)
    {
        interest = EPOLLIN | (session->tlsWantWrite ? EPOLLOUT : 0);
    }
    if(HasPendingSend(session))
    {
        interest |= EPOLLOUT;
    }
    if(interest != session->interest)
    {
        struct epoll_event event = {.events = interest, .data.ptr = session};

        if(epoll_ctl(epollFd, EPOLL_CTL_MOD, session->fd, &event) == 0)
        {
            session->interest = interest;
        }
    }
}
