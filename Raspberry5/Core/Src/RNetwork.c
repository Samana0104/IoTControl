#include "RNetwork.h"
#include "IoTPacketStream.h"
#include "RBluetooth.h"
#include "RLog.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NET_MAX_CONNECTIONS MAX_SESSION
#define NET_RX_BUFFER_SIZE (PACKET_FRAME_SIZE * 2)
#define NET_TX_BUFFER_SIZE (PACKET_FRAME_SIZE * 4)
// epoll_wait 최대 대기이자 타임아웃 확인 주기
#define NET_TICK_MS 500
// 프레임 첫 바이트를 받은 뒤 프레임 전체가 도착해야 하는 시간
#define FRAME_TIMEOUT_MS 5000
// 송신이 이 시간 동안 진행되지 않으면 상대가 받지 않는 것으로 보고 끊음
#define SEND_STALL_TIMEOUT_MS 5000
// handler가 닫기를 요청한 뒤 남은 송신(예: 실패 ACK)을 내보내는 최대 시간
#define CLOSE_FLUSH_TIMEOUT_MS 2000

typedef enum
{
    NET_STATE_OPEN,
    NET_STATE_CLOSING
} RNetState;

// 연결 하나. EPOLLONESHOT으로 받은 워커 하나가 owned 동안 맡음.
typedef struct _RNetConnection
{
    // tableMutex 보호
    int inUse;
    uint32_t generation; // 슬롯 재사용 후 늦게 도착한 이벤트를 걸러냄

    // lock 보호
    pthread_mutex_t lock;
    RSessionType type;
    int fd;
    RSession *session;
    char address[SESSION_ADDRESS_SIZE];
    RNetState state;
    int owned;
    int closeRequested;
    uint64_t deadlineMs; // NET_STATE_CLOSING 마감
    uint64_t frameStartMs;
    uint8_t rx[NET_RX_BUFFER_SIZE];
    size_t rxLength;
    uint8_t tx[NET_TX_BUFFER_SIZE];
    size_t txLength;
    uint64_t txProgressMs;
} RNetConnection;

static RNetConnection connections[NET_MAX_CONNECTIONS];
static int connectionCount;
static int netStopping;
static int workersStopping;
static int netInitialized;
static uint64_t lastTimeoutCheckMs;
// 슬롯 inUse/generation과 개수 보호. 잠금 순서: tableMutex → 연결 lock
static pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t idleCond = PTHREAD_COND_INITIALIZER;
static RNetFrameHandler frameHandler;
static int epollFd = -1;
static pthread_t workers[NET_MAX_WORKERS];
static int workerCount;

static uint64_t GetMonotonicMs(void);
static RNetConnection *ClaimConnection(RSessionType type, int fd);
static RNetConnection *FindConnection(int fd);
static int CloseTcpConnectionFrom(const char *ip);
static uint64_t MakeEventData(const RNetConnection *connection);
static int RegisterConnection(RNetConnection *connection);
static void RearmConnection(RNetConnection *connection);
static void CloseConnection(RNetConnection *connection);
static void *RunWorker(void *arg);
static void HandleConnection(uint64_t eventData, uint32_t events);
static int ServiceConnection(RNetConnection *connection, uint32_t events);
static int ReceiveFrames(RNetConnection *connection);
static int ProcessBufferedFrame(RNetConnection *connection);
static int FlushConnection(RNetConnection *connection);
static int ReadSocket(RNetConnection *connection);
static void CloseSocket(RSessionType type, int fd);
static void CheckTimeouts(void);
static int IsConnectionTimedOut(RNetConnection *connection, uint64_t now);

int RNetStart(RNetFrameHandler handler, int count)
{
    if(handler == NULL || count < 1 || count > NET_MAX_WORKERS)
    {
        errno = EINVAL;
        return -1;
    }
    if(!netInitialized)
    {
        for(int index = 0; index < NET_MAX_CONNECTIONS; ++index)
        {
            if(pthread_mutex_init(&connections[index].lock, NULL) != 0)
            {
                return -1;
            }
        }
        netInitialized = 1;
    }
    frameHandler = handler;
    netStopping = 0;
    workersStopping = 0;
    epollFd = epoll_create1(EPOLL_CLOEXEC);
    if(epollFd < 0)
    {
        RLOG_ERROR("epoll_create1: %s", strerror(errno));
        return -1;
    }
    for(workerCount = 0; workerCount < count; ++workerCount)
    {
        int createResult = pthread_create(&workers[workerCount], NULL, RunWorker, NULL);

        if(createResult != 0)
        {
            RLOG_ERROR("pthread_create(network worker): %s", strerror(createResult));
            RNetStop();
            errno = createResult;
            return -1;
        }
    }
    return 0;
}

void RNetStop(void)
{
    if(epollFd < 0)
    {
        return;
    }
    pthread_mutex_lock(&tableMutex);
    netStopping = 1;
    for(int index = 0; index < NET_MAX_CONNECTIONS; ++index)
    {
        RNetConnection *connection = &connections[index];

        if(connection->inUse)
        {
            pthread_mutex_lock(&connection->lock);
            connection->closeRequested = 1;
            shutdown(connection->fd, SHUT_RDWR);
            pthread_mutex_unlock(&connection->lock);
        }
    }
    // 워커가 끊김 이벤트를 받아 닫음. 처리 중인 handler는 끝난 뒤 닫힘.
    while(connectionCount > 0)
    {
        pthread_cond_wait(&idleCond, &tableMutex);
    }
    workersStopping = 1;
    pthread_mutex_unlock(&tableMutex);

    for(int index = 0; index < workerCount; ++index)
    {
        pthread_join(workers[index], NULL);
    }
    workerCount = 0;
    close(epollFd);
    epollFd = -1;
}

int RNetOpenTcp(int fd, const struct sockaddr_in *address)
{
    RNetConnection *connection = NULL;
    char ip[INET_ADDRSTRLEN] = "";
    int previousFd = -1;

    if(fd < 0 || address == NULL)
    {
        RLOG_ERROR("RNetOpenTcp: invalid argument: fd=%d", fd);
        if(fd >= 0)
        {
            close(fd);
        }
        return -1;
    }
    inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip));
    if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0)
    {
        pthread_mutex_lock(&tableMutex);
        // 같은 IP의 기존 연결은 닫음 (재접속한 클라이언트의 끊긴 연결이 남아 있는 경우가 대부분)
        previousFd = CloseTcpConnectionFrom(ip);
        connection = ClaimConnection(SESSION_TCP, fd);
        if(connection != NULL)
        {
            snprintf(connection->address, sizeof(connection->address), "%s", ip);
            connection->session = RSessionAdd(SESSION_TCP, fd, ip, NULL, MEMBER_TYPE_UNKNOWN);
            if(connection->session == NULL)
            {
                connection->inUse = 0;
                connectionCount--;
                connection = NULL;
            }
        }
        pthread_mutex_unlock(&tableMutex);
    }
    if(connection == NULL)
    {
        RLOG_WARN("TCP client rejected: ip=%s (connection limit reached or server stopping)", ip);
        close(fd);
        return -1;
    }
    if(previousFd >= 0)
    {
        RLOG_WARN("Duplicate connection from %s: closing previous fd=%d", ip, previousFd);
    }
    RLOG_INFO("Client connected: ip=%s, fd=%d", ip, fd);
    return RegisterConnection(connection);
}

int RNetOpenBt(int fd, const char *memberId, const char *mac, RMemberType memberType)
{
    RNetConnection *connection = NULL;
    int openError = EINVAL;

    if(memberId != NULL && mac != NULL && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0)
    {
        pthread_mutex_lock(&tableMutex);
        connection = ClaimConnection(SESSION_BLUETOOTH, fd);
        openError = netStopping ? ECANCELED : ENOSPC;
        if(connection != NULL)
        {
            snprintf(connection->address, sizeof(connection->address), "%s", mac);
            connection->session = RSessionAdd(SESSION_BLUETOOTH, fd, mac, memberId, memberType);
            if(connection->session == NULL)
            {
                connection->inUse = 0;
                connectionCount--;
                connection = NULL;
            }
        }
        pthread_mutex_unlock(&tableMutex);
    }
    if(connection == NULL)
    {
        DisconnectBluetoothDevice(fd);
        errno = openError;
        return -1;
    }
    return RegisterConnection(connection);
}

int RNetSend(int fd, const void *frame, size_t frameLength)
{
    RNetConnection *connection;
    int result = 0;

    if(fd < 0 || frame == NULL || frameLength == 0)
    {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    connection = FindConnection(fd);
    if(connection != NULL)
    {
        pthread_mutex_lock(&connection->lock);
    }
    pthread_mutex_unlock(&tableMutex);
    if(connection == NULL)
    {
        return 1;
    }

    if(connection->state != NET_STATE_OPEN || connection->closeRequested)
    {
        result = 1;
    }
    else if(connection->txLength + frameLength > sizeof(connection->tx))
    {
        errno = ENOBUFS;
        result = -1;
    }
    else
    {
        if(connection->txLength == 0)
        {
            connection->txProgressMs = GetMonotonicMs();
        }
        memcpy(connection->tx + connection->txLength, frame, frameLength);
        connection->txLength += frameLength;
        if(FlushConnection(connection) != 0)
        {
            connection->closeRequested = 1;
            shutdown(connection->fd, SHUT_RDWR);
            errno = ECONNRESET;
            result = -1;
        }
        // 맡은 워커가 없으면 남은 송신을 위해 쓰기 가능 이벤트를 걸어 둠 (맡은 워커는 끝날 때 스스로 검)
        else if(connection->txLength > 0 && !connection->owned)
        {
            RearmConnection(connection);
        }
    }
    pthread_mutex_unlock(&connection->lock);
    return result;
}

int RNetClose(int fd)
{
    RNetConnection *connection;

    pthread_mutex_lock(&tableMutex);
    connection = FindConnection(fd);
    if(connection != NULL)
    {
        pthread_mutex_lock(&connection->lock);
        connection->closeRequested = 1;
        shutdown(connection->fd, SHUT_RDWR);
        pthread_mutex_unlock(&connection->lock);
    }
    pthread_mutex_unlock(&tableMutex);
    return connection != NULL ? 0 : 1;
}

static uint64_t GetMonotonicMs(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

// tableMutex를 잡은 상태에서 호출. 종료 중이거나 가득 차면 NULL
static RNetConnection *ClaimConnection(RSessionType type, int fd)
{
    if(netStopping)
    {
        return NULL;
    }
    for(int index = 0; index < NET_MAX_CONNECTIONS; ++index)
    {
        RNetConnection *connection = &connections[index];

        if(connection->inUse)
        {
            continue;
        }
        memset(connection->address, 0, sizeof(connection->address));

        connection->inUse = 1;
        connection->generation++;
        connection->type = type;
        connection->fd = fd;
        connection->session = NULL;
        connection->state = NET_STATE_OPEN;
        connection->owned = 0;
        connection->closeRequested = 0;
        connection->deadlineMs = 0;
        connection->frameStartMs = 0;
        connection->rxLength = 0;
        connection->txLength = 0;
        connectionCount++;
        return connection;
    }
    return NULL;
}

// tableMutex를 잡은 상태에서 호출
static RNetConnection *FindConnection(int fd)
{
    for(int index = 0; index < NET_MAX_CONNECTIONS; ++index)
    {
        if(connections[index].inUse && connections[index].fd == fd)
        {
            return &connections[index];
        }
    }
    return NULL;
}

// tableMutex를 잡은 상태에서 호출. 같은 IP의 TCP 연결을 닫도록 요청 (맡은 워커가 정리). 닫은 fd, 없으면 -1
static int CloseTcpConnectionFrom(const char *ip)
{
    for(int index = 0; index < NET_MAX_CONNECTIONS; ++index)
    {
        RNetConnection *connection = &connections[index];
        int fd = -1;

        if(!connection->inUse || connection->type != SESSION_TCP)
        {
            continue;
        }
        pthread_mutex_lock(&connection->lock);
        if(!connection->closeRequested && strcmp(connection->address, ip) == 0)
        {
            connection->closeRequested = 1;
            shutdown(connection->fd, SHUT_RDWR);
            fd = connection->fd;
        }
        pthread_mutex_unlock(&connection->lock);
        if(fd >= 0)
        {
            return fd;
        }
    }
    return -1;
}

// epoll 이벤트에 슬롯 번호와 세대를 담음
static uint64_t MakeEventData(const RNetConnection *connection)
{
    return ((uint64_t)connection->generation << 32) | (uint64_t)(connection - connections);
}

static int RegisterConnection(RNetConnection *connection)
{
    struct epoll_event event = {.events = EPOLLIN | EPOLLONESHOT};

    pthread_mutex_lock(&connection->lock);
    event.data.u64 = MakeEventData(connection);
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, connection->fd, &event) != 0)
    {
        int registerError = errno;

        RLOG_ERROR("epoll_ctl(ADD): fd=%d: %s", connection->fd, strerror(registerError));
        connection->owned = 1;
        pthread_mutex_unlock(&connection->lock);
        CloseConnection(connection);
        errno = registerError;
        return -1;
    }
    pthread_mutex_unlock(&connection->lock);
    return 0;
}

// 연결 lock을 잡은 상태에서 호출. 상태에 맞는 이벤트로 다시 등록 (ONESHOT)
static void RearmConnection(RNetConnection *connection)
{
    struct epoll_event event = {.data.u64 = MakeEventData(connection)};
    uint32_t interest = 0;

    if(connection->state == NET_STATE_OPEN)
    {
        interest = EPOLLIN;
    }
    if(connection->txLength > 0)
    {
        interest |= EPOLLOUT;
    }
    event.events = interest | EPOLLONESHOT;
    epoll_ctl(epollFd, EPOLL_CTL_MOD, connection->fd, &event);
}

// 맡은 워커(owned)만 호출. epoll에서 빼고 세션을 지운 뒤 fd를 닫음
static void CloseConnection(RNetConnection *connection)
{
    RSessionType type;
    RSession *session;
    char address[SESSION_ADDRESS_SIZE];
    int fd;
    int remaining;

    pthread_mutex_lock(&tableMutex);
    pthread_mutex_lock(&connection->lock);
    epoll_ctl(epollFd, EPOLL_CTL_DEL, connection->fd, NULL);
    type = connection->type;
    fd = connection->fd;
    session = connection->session;
    memcpy(address, connection->address, sizeof(address));
    connection->session = NULL;
    connection->fd = -1;
    explicit_bzero(connection->rx, connection->rxLength);
    connection->rxLength = 0;
    connection->txLength = 0;
    connection->owned = 0;
    connection->inUse = 0;
    remaining = --connectionCount;
    pthread_mutex_unlock(&connection->lock);

    // fd 번호가 재사용되기 전에 세션부터 지움
    if(session != NULL)
    {
        RSessionRemove(session);
    }
    CloseSocket(type, fd);
    RLOG_INFO("Connection closed: %s, fd=%d, connections=%d", address, fd, remaining);
    pthread_cond_broadcast(&idleCond);
    pthread_mutex_unlock(&tableMutex);
}

static void *RunWorker(void *arg)
{
    (void)arg;
    while(1)
    {
        struct epoll_event event;
        int eventCount;
        int stopping;

        pthread_mutex_lock(&tableMutex);
        stopping = workersStopping;
        pthread_mutex_unlock(&tableMutex);
        if(stopping)
        {
            break;
        }
        // 한 번에 하나만 받음: handler가 오래 걸려도 다른 연결은 다른 워커가 처리
        eventCount = epoll_wait(epollFd, &event, 1, NET_TICK_MS);
        if(eventCount > 0)
        {
            HandleConnection(event.data.u64, event.events);
        }
        else if(eventCount < 0 && errno != EINTR)
        {
            RLOG_ERROR("epoll_wait: %s", strerror(errno));
        }
        CheckTimeouts();
    }
    return NULL;
}

static void HandleConnection(uint64_t eventData, uint32_t events)
{
    RNetConnection *connection = &connections[eventData & 0xFFFFFFFFU];
    int result;

    pthread_mutex_lock(&tableMutex);
    if(!connection->inUse || connection->generation != (uint32_t)(eventData >> 32))
    {
        pthread_mutex_unlock(&tableMutex);
        return;
    }
    pthread_mutex_lock(&connection->lock);
    pthread_mutex_unlock(&tableMutex);
    // RNetSend의 재등록과 겹쳐 이벤트가 두 번 올 수 있음. 이미 맡은 워커가 있으면 그쪽이 처리
    if(connection->owned)
    {
        pthread_mutex_unlock(&connection->lock);
        return;
    }
    connection->owned = 1;

    result = ServiceConnection(connection, events);
    if(result != 0)
    {
        pthread_mutex_unlock(&connection->lock);
        CloseConnection(connection);
        return;
    }
    connection->owned = 0;
    RearmConnection(connection);
    pthread_mutex_unlock(&connection->lock);
}

// 연결 lock을 잡은 상태 (handler 호출 동안만 풀림). 0: 유지, -1: 닫기
static int ServiceConnection(RNetConnection *connection, uint32_t events)
{
    if(connection->closeRequested || (events & EPOLLERR))
    {
        return -1;
    }
    if(connection->state == NET_STATE_OPEN && ReceiveFrames(connection) != 0)
    {
        return -1;
    }
    if(FlushConnection(connection) != 0)
    {
        return -1;
    }
    // handler 중 RNetClose가 왔거나, 닫기 전 송신을 다 보냄
    if(connection->closeRequested || (connection->state == NET_STATE_CLOSING && connection->txLength == 0))
    {
        return -1;
    }
    return 0;
}

// 버퍼의 프레임을 모두 처리하고, 모자라면 더 읽음. -1: 끊김 또는 깨진 프레임
static int ReceiveFrames(RNetConnection *connection)
{
    while(connection->state == NET_STATE_OPEN)
    {
        int frameResult = ProcessBufferedFrame(connection);
        int readResult;

        if(frameResult < 0)
        {
            return -1;
        }
        if(frameResult > 0)
        {
            continue;
        }
        readResult = ReadSocket(connection);
        if(readResult <= 0)
        {
            return readResult;
        }
    }
    return 0;
}

// 1: 프레임 하나 처리, 0: 데이터 부족, -1: 깨진 프레임
static int ProcessBufferedFrame(RNetConnection *connection)
{
    const char *label = connection->session->label;
    HeaderData header;
    size_t frameSize;
    int result;

    if(connection->rxLength < HEADER_SIZE)
    {
        return 0;
    }
    DecodePacketHeader(connection->rx, &header);
    if(header.length > MAX_PAYLOAD_SIZE)
    {
        RLOG_WARN("[%s] Invalid data length: cmd=0x%04X, length=%u", label, (unsigned int)header.cmd, (unsigned int)header.length);
        return -1;
    }
    frameSize = HEADER_SIZE + header.length;
    if(connection->rxLength < frameSize)
    {
        return 0;
    }
    if(CheckPacketCrc(connection->rx, &header, connection->rx + HEADER_SIZE) != 0)
    {
        RLOG_WARN("[%s] CRC mismatch: cmd=0x%04X", label, (unsigned int)header.cmd);
        return -1;
    }

    // owned 동안 rx는 이 워커만 사용. handler 중에도 RNetSend가 tx에 넣을 수 있게 lock을 풂
    pthread_mutex_unlock(&connection->lock);
    result = frameHandler(connection->session, header.cmd, connection->rx + HEADER_SIZE, header.length);
    pthread_mutex_lock(&connection->lock);

    connection->rxLength -= frameSize;
    memmove(connection->rx, connection->rx + frameSize, connection->rxLength);
    // 로그인 비밀번호 등이 버퍼에 남지 않게 지움
    explicit_bzero(connection->rx + connection->rxLength, frameSize);
    connection->frameStartMs = connection->rxLength > 0 ? GetMonotonicMs() : 0;
    if(result != 0)
    {
        RLOG_WARN("[%s] Packet handler rejected frame: cmd=0x%04X, length=%u, result=%d; closing connection", label, (unsigned int)header.cmd, (unsigned int)header.length, result);
        connection->state = NET_STATE_CLOSING;
        connection->deadlineMs = GetMonotonicMs() + CLOSE_FLUSH_TIMEOUT_MS;
    }
    return 1;
}

// 연결 lock을 잡은 상태에서 호출. 빈 수신 버퍼 공간만큼 읽음. >0: 읽은 바이트 수, 0: 지금 읽을 것 없음, -1: 끊김/오류
static int ReadSocket(RNetConnection *connection)
{
    size_t space = sizeof(connection->rx) - connection->rxLength;
    ssize_t result;

    if(space == 0)
    {
        return 0;
    }
    result = recv(connection->fd, connection->rx + connection->rxLength, space, MSG_DONTWAIT);
    if(result > 0)
    {
        if(connection->rxLength == 0)
        {
            connection->frameStartMs = GetMonotonicMs();
        }
        connection->rxLength += (size_t)result;
        return (int)result;
    }
    if(result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    {
        return 0;
    }
    return -1;
}

// 연결 lock을 잡은 상태에서 호출. 소켓이 받는 만큼 보냄. -1: 연결 오류
static int FlushConnection(RNetConnection *connection)
{
    while(connection->txLength > 0)
    {
        ssize_t sentLength = send(connection->fd, connection->tx, connection->txLength, MSG_DONTWAIT | MSG_NOSIGNAL);

        if(sentLength < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
        }
        connection->txLength -= (size_t)sentLength;
        memmove(connection->tx, connection->tx + sentLength, connection->txLength);
        connection->txProgressMs = GetMonotonicMs();
    }
    return 0;
}

static void CloseSocket(RSessionType type, int fd)
{
    if(type == SESSION_BLUETOOTH)
    {
        DisconnectBluetoothDevice(fd);
        return;
    }
    close(fd);
}

// 주기마다 워커 하나가 확인. 시간이 지난 연결은 shutdown해서 맡은 워커가 닫게 함
static void CheckTimeouts(void)
{
    uint64_t now = GetMonotonicMs();

    pthread_mutex_lock(&tableMutex);
    if(now - lastTimeoutCheckMs < NET_TICK_MS)
    {
        pthread_mutex_unlock(&tableMutex);
        return;
    }
    lastTimeoutCheckMs = now;
    for(int index = 0; index < NET_MAX_CONNECTIONS; ++index)
    {
        RNetConnection *connection = &connections[index];

        if(!connection->inUse)
        {
            continue;
        }
        pthread_mutex_lock(&connection->lock);
        if(!connection->owned && !connection->closeRequested && IsConnectionTimedOut(connection, now))
        {
            connection->closeRequested = 1;
            shutdown(connection->fd, SHUT_RDWR);
        }
        pthread_mutex_unlock(&connection->lock);
    }
    pthread_mutex_unlock(&tableMutex);
}

static int IsConnectionTimedOut(RNetConnection *connection, uint64_t now)
{
    const char *label = connection->session != NULL ? connection->session->label : connection->address;

    if(connection->state == NET_STATE_CLOSING && now >= connection->deadlineMs)
    {
        return 1;
    }
    if(connection->state == NET_STATE_OPEN && connection->frameStartMs != 0 && now - connection->frameStartMs >= FRAME_TIMEOUT_MS)
    {
        RLOG_WARN("[%s] Frame timeout", label);
        return 1;
    }
    if(connection->txLength > 0 && now - connection->txProgressMs >= SEND_STALL_TIMEOUT_MS)
    {
        RLOG_WARN("[%s] Send stalled", label);
        return 1;
    }
    return 0;
}
