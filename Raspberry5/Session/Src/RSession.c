#include "RSession.h"
#include "RLog.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#define SESSION_SEND_BUFFER_SIZE PACKET_FRAME_SIZE
#define SESSION_LABEL_SIZE 64
#define BT_CONNECT_TIMEOUT_MS 5000
#define BT_SEND_TIMEOUT_MS 2000
#define TCP_SEND_TIMEOUT_MS 5000

// fd 하나 = 세션 하나. refCount(작업 스레드 + 진행 중인 송신)가 0이 되면 fd를 닫고 슬롯 반환.
struct _RSession
{
    int index;
    int inUse;
    int refCount;
    RSessionType type;
    int fd;
    char address[SESSION_ADDRESS_SIZE];
    char label[SESSION_LABEL_SIZE];
    char memberId[MEM_ID_SIZE + 1];
    int authenticated;

    // TCP: SSL 호출 보호, BT: 송신 직렬화
    pthread_mutex_t ioMutex;
    SSL *tls;     // TCP 전용
    int stopFd;   // BT 전용: 수신/송신 대기를 깨우는 eventfd

    // connected와 TCP 송신 큐 보호. TCP는 송신 스레드가 큐를 비움.
    pthread_mutex_t sendMutex;
    pthread_cond_t sendCond;
    int connected;
    uint8_t sendData[SESSION_SEND_BUFFER_SIZE];
    size_t sendLength;
    int sendPending;
    int sendComplete;
    int sendResult;
};

static RSession sessions[MAX_SESSION];
static int sessionCount;
static int cleanupCount;
static int sessionClosing;
static int sessionInitialized;
// 슬롯 상태(inUse/refCount/memberId/authenticated)와 개수 보호. 잠금 순서: tableMutex → sendMutex
static pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sessionIdleCond = PTHREAD_COND_INITIALIZER;
// BT 연결/교체 직렬화
static pthread_mutex_t btConnectMutex = PTHREAD_MUTEX_INITIALIZER;
static RSessionReceiver sessionReceiver;

static RSession *ClaimSession(RSessionType type, int fd, int refCount);
static int IsSessionConnected(RSession *session);
static void StopSession(RSession *session);
static void ReleaseSession(RSession *session);
static int StartSessionThread(RSession *session, void *(*routine)(void *));
static void *ReceiveSessionThread(void *arg);
static void *SendTcpThread(void *arg);
static int WaitForTcpSocket(RSession *session, int tlsError, int timeoutMs);
static int ReceiveTcpData(RSession *session, void *buffer, size_t length);
static int WaitForTcpData(RSession *session, int timeoutMs);
static int WriteTls(RSession *session, const void *buffer, size_t length);
static int SendTcpData(RSession *session, const void *buffer, size_t length);
static int WaitForBtEvent(RSession *session, short events, int timeoutMs);
static int ReceiveBtData(RSession *session, void *buffer, size_t length);
static int SendBtData(RSession *session, const void *buffer, size_t length);

int RSessionInit(RSessionReceiver receiver)
{
    if(receiver == NULL)
    {
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    sessionReceiver = receiver;
    sessionClosing = 0;
    if(!sessionInitialized)
    {
        for(int index = 0; index < MAX_SESSION; ++index)
        {
            RSession *session = &sessions[index];

            session->index = index;
            session->fd = -1;
            session->stopFd = -1;
            if(pthread_mutex_init(&session->ioMutex, NULL) != 0 || pthread_mutex_init(&session->sendMutex, NULL) != 0 || pthread_cond_init(&session->sendCond, NULL) != 0)
            {
                pthread_mutex_unlock(&tableMutex);
                RLOG_ERROR("Session synchronization initialization failed");
                return -1;
            }
        }
        sessionInitialized = 1;
    }
    pthread_mutex_unlock(&tableMutex);
    return 0;
}

void RSessionCloseAll(void)
{
    pthread_mutex_lock(&tableMutex);
    sessionClosing = 1;
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        if(sessions[index].inUse)
        {
            StopSession(&sessions[index]);
        }
    }
    while(sessionCount > 0 || cleanupCount > 0)
    {
        pthread_cond_wait(&sessionIdleCond, &tableMutex);
    }
    pthread_mutex_unlock(&tableMutex);
}

int RSessionOpenTcp(int fd, SSL *tls, const struct sockaddr_in *address)
{
    RSession *session;
    int count;

    // SSL 호출 중에는 ioMutex를 잡으므로 블로킹 대기는 잠금 밖의 poll에서 함
    if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0)
    {
        RLOG_ERROR("fcntl(O_NONBLOCK): %s", strerror(errno));
        SSL_shutdown(tls);
        SSL_free(tls);
        close(fd);
        return -1;
    }

    pthread_mutex_lock(&tableMutex);
    // 송신 스레드 + 수신 스레드
    session = ClaimSession(SESSION_TCP, fd, 2);
    if(session != NULL)
    {
        session->tls = tls;
        inet_ntop(AF_INET, &address->sin_addr, session->address, sizeof(session->address));
        snprintf(session->label, sizeof(session->label), "%s", session->address);
    }
    count = sessionCount;
    pthread_mutex_unlock(&tableMutex);
    if(session == NULL)
    {
        RLOG_WARN("Session limit reached; TCP client rejected");
        SSL_shutdown(tls);
        SSL_free(tls);
        close(fd);
        return -1;
    }

    if(StartSessionThread(session, SendTcpThread) != 0)
    {
        StopSession(session);
        ReleaseSession(session);
        ReleaseSession(session);
        return -1;
    }
    if(StartSessionThread(session, ReceiveSessionThread) != 0)
    {
        StopSession(session);
        ReleaseSession(session);
        return -1;
    }
    RLOG_INFO("Client connected: ip=%s, fd=%d, sessions=%d", session->address, fd, count);
    return 0;
}

int RSessionOpenBt(const char *memberId, const char *mac)
{
    RSession *previous = NULL;
    RSession *session;
    size_t memberIdLength;
    uint8_t rfcommChannel;
    int stopFd;
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

        if(!candidate->inUse || candidate->type != SESSION_BLUETOOTH || !IsSessionConnected(candidate))
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
            // 등록 MAC이 바뀜: 이전 세션은 새 연결 전에 끊음
            previous = candidate;
            previous->refCount++;
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
    pthread_mutex_unlock(&tableMutex);
    if(previous != NULL)
    {
        StopSession(previous);
        ReleaseSession(previous);
    }

    stopFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if(stopFd < 0)
    {
        int eventError = errno;

        pthread_mutex_unlock(&btConnectMutex);
        errno = eventError;
        return -1;
    }
    fd = ConnectBluetoothDevice(mac, BT_CONNECT_TIMEOUT_MS, &rfcommChannel);
    if(fd < 0)
    {
        int connectError = errno;

        RLOG_WARN("[BT] HC-05 connection failed: id=%s, mac=%s: %s", memberId, mac, strerror(connectError));
        close(stopFd);
        pthread_mutex_unlock(&btConnectMutex);
        errno = connectError;
        return -1;
    }

    pthread_mutex_lock(&tableMutex);
    // 수신 스레드
    session = sessionClosing ? NULL : ClaimSession(SESSION_BLUETOOTH, fd, 1);
    if(session != NULL)
    {
        session->stopFd = stopFd;
        session->authenticated = 1;
        memcpy(session->memberId, memberId, memberIdLength + 1);
        memcpy(session->address, mac, BLUETOOTH_MAC_SIZE);
        session->address[BLUETOOTH_MAC_SIZE] = '\0';
        snprintf(session->label, sizeof(session->label), "BT id=%s mac=%s", session->memberId, session->address);
    }
    else
    {
        errno = sessionClosing ? ECANCELED : ENOSPC;
    }
    pthread_mutex_unlock(&tableMutex);
    if(session == NULL)
    {
        int claimError = errno;

        RLOG_WARN("[BT] Session not available: id=%s: %s", memberId, strerror(claimError));
        DisconnectBluetoothDevice(fd);
        close(stopFd);
        pthread_mutex_unlock(&btConnectMutex);
        errno = claimError;
        return -1;
    }

    if(StartSessionThread(session, ReceiveSessionThread) != 0)
    {
        int threadError = errno;

        StopSession(session);
        ReleaseSession(session);
        pthread_mutex_unlock(&btConnectMutex);
        errno = threadError;
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

    // 송신하는 동안 세션이 닫혀도 fd가 재사용되지 않게 참조를 잡음
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        if(sessions[index].inUse && sessions[index].fd == fd)
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
    if(!IsSessionConnected(session))
    {
        ReleaseSession(session);
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
        RSession *session = &sessions[index];

        if(session->inUse && session->type == SESSION_BLUETOOTH && strcmp(session->memberId, memberId) == 0 && IsSessionConnected(session))
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
        RSession *session = &sessions[index];
        RSessionSnapshot *snapshot;

        if(!session->inUse)
        {
            continue;
        }
        snapshot = &snapshots[snapshotCount++];
        snapshot->index = session->index;
        snapshot->type = session->type;
        snapshot->fd = session->fd;
        snapshot->authenticated = session->authenticated;
        snapshot->connected = IsSessionConnected(session);
        memcpy(snapshot->address, session->address, sizeof(snapshot->address));
        memcpy(snapshot->memberId, session->memberId, sizeof(snapshot->memberId));
    }
    pthread_mutex_unlock(&tableMutex);
    return snapshotCount;
}

int RSessionReceive(RSession *session, void *buffer, size_t length)
{
    return session->type == SESSION_TCP ? ReceiveTcpData(session, buffer, length) : ReceiveBtData(session, buffer, length);
}

int RSessionWaitForData(RSession *session, int timeoutMs)
{
    return session->type == SESSION_TCP ? WaitForTcpData(session, timeoutMs) : WaitForBtEvent(session, POLLIN, timeoutMs);
}

int RSessionSendFrame(RSession *session, const void *frame, size_t frameLength)
{
    return session->type == SESSION_TCP ? SendTcpData(session, frame, frameLength) : SendBtData(session, frame, frameLength);
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

// tableMutex를 잡은 상태에서 호출. 빈 슬롯을 초기화해 반환, 없으면 NULL.
static RSession *ClaimSession(RSessionType type, int fd, int refCount)
{
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        RSession *session = &sessions[index];

        if(session->inUse)
        {
            continue;
        }
        session->inUse = 1;
        session->refCount = refCount;
        session->type = type;
        session->fd = fd;
        session->tls = NULL;
        session->stopFd = -1;
        session->authenticated = 0;
        memset(session->address, 0, sizeof(session->address));
        memset(session->label, 0, sizeof(session->label));
        memset(session->memberId, 0, sizeof(session->memberId));
        pthread_mutex_lock(&session->sendMutex);
        session->connected = 1;
        session->sendLength = 0;
        session->sendPending = 0;
        session->sendComplete = 0;
        session->sendResult = 0;
        pthread_mutex_unlock(&session->sendMutex);
        sessionCount++;
        return session;
    }
    return NULL;
}

static int IsSessionConnected(RSession *session)
{
    int connected;

    pthread_mutex_lock(&session->sendMutex);
    connected = session->connected;
    pthread_mutex_unlock(&session->sendMutex);
    return connected;
}

// 연결 중지 신호. fd는 마지막 참조가 ReleaseSession할 때 닫힘.
static void StopSession(RSession *session)
{
    int shouldStop = 0;

    pthread_mutex_lock(&session->sendMutex);
    if(session->connected)
    {
        session->connected = 0;
        shouldStop = 1;
    }
    pthread_cond_broadcast(&session->sendCond);
    pthread_mutex_unlock(&session->sendMutex);

    if(!shouldStop)
    {
        return;
    }
    if(session->type == SESSION_TCP)
    {
        shutdown(session->fd, SHUT_RDWR);
    }
    else
    {
        uint64_t stopSignal = 1;
        ssize_t writeResult;

        do
        {
            writeResult = write(session->stopFd, &stopSignal, sizeof(stopSignal));
        }
        while(writeResult < 0 && errno == EINTR);
        if(writeResult < 0 && errno != EAGAIN)
        {
            RLOG_ERROR("[%s] Stop signal failed: %s", session->label, strerror(errno));
            shutdown(session->fd, SHUT_RDWR);
        }
    }
}

// 참조 하나 반환. 마지막 참조가 fd/TLS/stopFd를 닫고 슬롯을 비움.
static void ReleaseSession(RSession *session)
{
    RSessionType type = session->type;
    int fd = -1;
    int stopFd = -1;
    SSL *tls = NULL;
    int remainingSessions = 0;
    char label[SESSION_LABEL_SIZE];

    pthread_mutex_lock(&tableMutex);
    if(--session->refCount == 0)
    {
        fd = session->fd;
        stopFd = session->stopFd;
        tls = session->tls;
        memcpy(label, session->label, sizeof(label));
        session->fd = -1;
        session->stopFd = -1;
        session->tls = NULL;
        session->authenticated = 0;
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
    if(stopFd >= 0)
    {
        close(stopFd);
    }
    RLOG_INFO("Session closed: %s, fd=%d, sessions=%d", label, fd, remainingSessions);

    pthread_mutex_lock(&tableMutex);
    --cleanupCount;
    pthread_cond_broadcast(&sessionIdleCond);
    pthread_mutex_unlock(&tableMutex);
}

static int StartSessionThread(RSession *session, void *(*routine)(void *))
{
    pthread_t thread;
    int createResult = pthread_create(&thread, NULL, routine, session);

    if(createResult != 0)
    {
        RLOG_ERROR("[%s] pthread_create: %s", session->label, strerror(createResult));
        errno = createResult;
        return -1;
    }
    pthread_detach(thread);
    return 0;
}

static void *ReceiveSessionThread(void *arg)
{
    RSession *session = (RSession *)arg;

    sessionReceiver(session);
    StopSession(session);
    ReleaseSession(session);
    return NULL;
}

/* ============================================================================
   TCP (TLS)
   ============================================================================ */

// SSL이 WANT_READ/WANT_WRITE를 돌려주면 잠금 없이 소켓을 기다림. 1: 다시 시도, 0: 시간 초과, -1: 오류
static int WaitForTcpSocket(RSession *session, int tlsError, int timeoutMs)
{
    struct pollfd socketEvent = {.fd = session->fd, .events = tlsError == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN};

    while(1)
    {
        int result = poll(&socketEvent, 1, timeoutMs);

        if(result > 0)
        {
            // 끊김(POLLHUP/POLLERR)도 다시 시도해서 SSL이 오류를 돌려주게 함
            return 1;
        }
        if(result == 0)
        {
            return 0;
        }
        if(errno != EINTR)
        {
            return -1;
        }
    }
}

static int ReceiveTcpData(RSession *session, void *buffer, size_t length)
{
    uint8_t *current = (uint8_t *)buffer;
    size_t receivedLength = 0;

    while(receivedLength < length)
    {
        int result;
        int tlsError;

        pthread_mutex_lock(&session->ioMutex);
        ERR_clear_error();
        result = SSL_read(session->tls, current + receivedLength, (int)(length - receivedLength));
        tlsError = result > 0 ? SSL_ERROR_NONE : SSL_get_error(session->tls, result);
        pthread_mutex_unlock(&session->ioMutex);

        if(result > 0)
        {
            receivedLength += (size_t)result;
            continue;
        }
        if(tlsError == SSL_ERROR_WANT_READ || tlsError == SSL_ERROR_WANT_WRITE)
        {
            if(WaitForTcpSocket(session, tlsError, -1) < 0)
            {
                return -1;
            }
            continue;
        }
        if(tlsError == SSL_ERROR_SYSCALL && errno == EINTR)
        {
            continue;
        }
        return -1;
    }
    return 0;
}

static int WaitForTcpData(RSession *session, int timeoutMs)
{
    struct pollfd socketEvent = {.fd = session->fd, .events = POLLIN};
    int pendingData;

    pthread_mutex_lock(&session->ioMutex);
    pendingData = SSL_pending(session->tls);
    pthread_mutex_unlock(&session->ioMutex);
    if(pendingData > 0)
    {
        return 1;
    }

    while(1)
    {
        int result = poll(&socketEvent, 1, timeoutMs);

        if(result > 0)
        {
            return (socketEvent.revents & POLLIN) ? 1 : -1;
        }
        if(result == 0)
        {
            return 0;
        }
        if(errno != EINTR)
        {
            return -1;
        }
    }
}

static int WriteTls(RSession *session, const void *buffer, size_t length)
{
    const uint8_t *current = (const uint8_t *)buffer;
    size_t sentLength = 0;

    while(sentLength < length)
    {
        int result;
        int tlsError;

        pthread_mutex_lock(&session->ioMutex);
        ERR_clear_error();
        result = SSL_write(session->tls, current + sentLength, (int)(length - sentLength));
        tlsError = result > 0 ? SSL_ERROR_NONE : SSL_get_error(session->tls, result);
        pthread_mutex_unlock(&session->ioMutex);

        if(result > 0)
        {
            sentLength += (size_t)result;
            continue;
        }
        if(tlsError == SSL_ERROR_WANT_READ || tlsError == SSL_ERROR_WANT_WRITE)
        {
            // 상대가 받지 않아 송신이 멈추면 연결을 끊음
            if(WaitForTcpSocket(session, tlsError, TCP_SEND_TIMEOUT_MS) <= 0)
            {
                return -1;
            }
            continue;
        }
        if(tlsError == SSL_ERROR_SYSCALL && errno == EINTR)
        {
            continue;
        }
        return -1;
    }
    return 0;
}

// 송신 스레드에 프레임을 넘기고 전송이 끝날 때까지 대기
static int SendTcpData(RSession *session, const void *buffer, size_t length)
{
    int result;

    if(length > sizeof(session->sendData))
    {
        errno = EMSGSIZE;
        return -1;
    }

    pthread_mutex_lock(&session->sendMutex);
    while(session->sendPending && session->connected)
    {
        pthread_cond_wait(&session->sendCond, &session->sendMutex);
    }
    if(!session->connected)
    {
        pthread_mutex_unlock(&session->sendMutex);
        errno = ENOTCONN;
        return -1;
    }

    memcpy(session->sendData, buffer, length);
    session->sendLength = length;
    session->sendPending = 1;
    session->sendComplete = 0;
    pthread_cond_broadcast(&session->sendCond);

    while(!session->sendComplete && session->connected)
    {
        pthread_cond_wait(&session->sendCond, &session->sendMutex);
    }
    result = session->sendComplete ? session->sendResult : -1;
    pthread_mutex_unlock(&session->sendMutex);
    if(result != 0)
    {
        errno = ECONNRESET;
    }
    return result;
}

static void *SendTcpThread(void *arg)
{
    RSession *session = (RSession *)arg;
    uint8_t sendBuffer[SESSION_SEND_BUFFER_SIZE];

    while(1)
    {
        size_t sendLength;
        int sendResult;

        pthread_mutex_lock(&session->sendMutex);
        while(!session->sendPending && session->connected)
        {
            pthread_cond_wait(&session->sendCond, &session->sendMutex);
        }
        if(!session->connected && !session->sendPending)
        {
            pthread_mutex_unlock(&session->sendMutex);
            break;
        }
        sendLength = session->sendLength;
        memcpy(sendBuffer, session->sendData, sendLength);
        pthread_mutex_unlock(&session->sendMutex);

        sendResult = WriteTls(session, sendBuffer, sendLength);

        pthread_mutex_lock(&session->sendMutex);
        session->sendResult = sendResult;
        session->sendPending = 0;
        session->sendComplete = 1;
        pthread_cond_broadcast(&session->sendCond);
        pthread_mutex_unlock(&session->sendMutex);

        if(sendResult != 0)
        {
            StopSession(session);
            break;
        }
    }

    ReleaseSession(session);
    return NULL;
}

/* ============================================================================
   Bluetooth (RFCOMM)
   ============================================================================ */

static int WaitForBtEvent(RSession *session, short events, int timeoutMs)
{
    struct pollfd pollEvents[2] =
    {
        {.fd = session->fd, .events = events},
        {.fd = session->stopFd, .events = POLLIN}
    };

    while(1)
    {
        int result = poll(pollEvents, sizeof(pollEvents) / sizeof(pollEvents[0]), timeoutMs);

        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if(result == 0)
        {
            return 0;
        }
        if(pollEvents[1].revents != 0)
        {
            errno = ECANCELED;
            return -1;
        }
        if(pollEvents[0].revents & events)
        {
            return 1;
        }
        if(pollEvents[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            errno = ECONNRESET;
            return -1;
        }
    }
}

static int ReceiveBtData(RSession *session, void *buffer, size_t length)
{
    uint8_t *current = (uint8_t *)buffer;
    size_t receivedLength = 0;

    while(receivedLength < length)
    {
        ssize_t result;

        if(WaitForBtEvent(session, POLLIN, -1) < 0)
        {
            return -1;
        }
        result = recv(session->fd, current + receivedLength, length - receivedLength, MSG_DONTWAIT);
        if(result > 0)
        {
            receivedLength += (size_t)result;
            continue;
        }
        if(result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        {
            continue;
        }
        if(result == 0)
        {
            errno = ECONNRESET;
        }
        return -1;
    }
    return 0;
}

// 수신 스레드의 ACK와 RSessionSend가 같은 소켓에 섞이지 않게 ioMutex로 직렬화
static int SendBtData(RSession *session, const void *buffer, size_t length)
{
    const uint8_t *current = (const uint8_t *)buffer;
    size_t sentLength = 0;
    int result = 0;

    pthread_mutex_lock(&session->ioMutex);
    while(sentLength < length)
    {
        ssize_t sendResult;
        int waitResult = WaitForBtEvent(session, POLLOUT, BT_SEND_TIMEOUT_MS);

        if(waitResult <= 0)
        {
            if(waitResult == 0)
            {
                errno = ETIMEDOUT;
            }
            result = -1;
            break;
        }
        sendResult = send(session->fd, current + sentLength, length - sentLength, MSG_DONTWAIT | MSG_NOSIGNAL);
        if(sendResult > 0)
        {
            sentLength += (size_t)sendResult;
            continue;
        }
        if(sendResult < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        {
            continue;
        }
        if(sendResult == 0)
        {
            errno = ECONNRESET;
        }
        result = -1;
        break;
    }
    pthread_mutex_unlock(&session->ioMutex);
    return result;
}
