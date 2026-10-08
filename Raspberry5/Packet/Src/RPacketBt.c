#include "RPacketBt.h"
#include "IoTPacketCodec.h"
#include "RBluetooth.h"
#include "RDatabase.h"
#include "RDatabaseQuery.h"
#include "RLog.h"
#include "RNetwork.h"
#include "RPacket.h"

#include <errno.h>
#include <pthread.h>
#include <sodium.h>
#include <string.h>
#include <strings.h>

#define BLUETOOTH_PAIR_TIMEOUT_SECONDS 30
#define BLUETOOTH_CONNECT_TIMEOUT_MS 5000

// BlueZ 에이전트 경로가 하나뿐이므로 페어링은 한 번에 하나씩
static pthread_mutex_t pairMutex = PTHREAD_MUTEX_INITIALIZER;
// 회원/MAC 중복 확인과 연결을 한 번에 하나씩 (BT 세션을 만드는 유일한 경로)
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;

static int RegisterDevice(const char *label, const char *memberId, const char *bluetoothMac, const char *pin);
static int ConnectRegisteredDevice(const char *memberId, const char *requestedMac);
static int ConnectDevice(const char *memberId, const char *mac);
static int FindBtSessionConflict(const char *memberId, const char *mac, int *previousFd);

int RPacketBtRegisterReceive(RSession *session, const uint8_t *payload, size_t length)
{
    BluetoothRegisterData registerData;
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    char pin[BLUETOOTH_PIN_SIZE + 1];
    size_t macLength;
    size_t pinLength;
    int registered;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketBtRegisterReceive: NULL argument");
        return -1;
    }
    if(!session->authenticated)
    {
        RLOG_WARN("[%s] HC-05 registration from unauthenticated session", session->label);
        return -1;
    }
    if(ReadBluetoothRegisterData(payload, length, &registerData) != 0)
    {
        RLOG_WARN("[%s] Malformed HC-05 registration payload: length=%zu", session->label, length);
        return -1;
    }
    macLength = strnlen(registerData.mac, BLUETOOTH_MAC_SIZE);
    pinLength = strnlen(registerData.pin, BLUETOOTH_PIN_SIZE);
    memcpy(bluetoothMac, registerData.mac, macLength);
    bluetoothMac[macLength] = '\0';
    memcpy(pin, registerData.pin, pinLength);
    pin[pinLength] = '\0';
    sodium_memzero(&registerData, sizeof(registerData));

    pthread_mutex_lock(&pairMutex);
    registered = RegisterDevice(session->label, session->memberId, bluetoothMac, pin) == 0;
    pthread_mutex_unlock(&pairMutex);
    sodium_memzero(pin, sizeof(pin));

    if(RPacketSendAck(session, REQ_BT_REGISTER, registered) != 0)
    {
        return -1;
    }
    return registered ? 0 : -1;
}

int RPacketBtConnectReceive(RSession *session, const uint8_t *payload, size_t length)
{
    BluetoothConnectData request;
    char memberId[MEM_ID_SIZE + 1];
    char requestedMac[BLUETOOTH_MAC_TEXT_SIZE];
    size_t memberIdLength;
    size_t passwordLength;
    int verifyResult;
    int connected = 0;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketBtConnectReceive: NULL argument");
        return -1;
    }
    if(session->type != SESSION_TCP || ReadBluetoothConnectData(payload, length, &request) != 0)
    {
        RLOG_WARN("[%s] Malformed Bluetooth connect request: length=%zu", session->label, length);
        return -1;
    }
    memberIdLength = strnlen(request.id, sizeof(request.id));
    passwordLength = strnlen(request.pw, sizeof(request.pw));
    memcpy(memberId, request.id, memberIdLength);
    memberId[memberIdLength] = '\0';
    memcpy(requestedMac, request.mac, sizeof(request.mac));
    requestedMac[sizeof(request.mac)] = '\0';
    verifyResult = memberIdLength > 0 && passwordLength > 0 && strnlen(request.mac, sizeof(request.mac)) == sizeof(request.mac) ? VerifyMember(request.id, memberIdLength, request.pw, passwordLength) : 0;
    sodium_memzero(&request, sizeof(request));

    if(verifyResult == 1 && ConnectRegisteredDevice(memberId, requestedMac) == 0)
    {
        connected = 1;
    }
    RLOG_INFO("[%s] Bluetooth request: id=%s, result=%s", session->label, verifyResult == 1 ? memberId : "-", connected ? "connected" : "failed");
    // BT 세션은 서버 소유라 요청한 클라이언트가 끊겨도 유지됨
    return RPacketSendAck(session, REQ_BT_CONNECT, connected);
}

int RPacketBtConnectMember(const char *memberId)
{
    if(memberId == NULL)
    {
        RLOG_ERROR("RPacketBtConnectMember: NULL memberId");
        errno = EINVAL;
        return -1;
    }
    return ConnectRegisteredDevice(memberId, NULL);
}

int RPacketBtRegisterMember(const char *memberId, const char *mac, const char *pin)
{
    DatabaseValue idParam[1];
    char existingId[MEM_ID_SIZE + 1];
    size_t memberIdLength;
    int queryResult;
    int registerResult = -1;

    if(memberId == NULL || mac == NULL || pin == NULL)
    {
        RLOG_ERROR("RPacketBtRegisterMember: NULL argument");
        return -1;
    }
    memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1);
    if(memberIdLength == 0 || memberIdLength > MEM_ID_SIZE)
    {
        RLOG_WARN("[CLI] Invalid member ID for HC-05 registration");
        return -1;
    }

    idParam[0] = DATABASE_TEXT(memberId);
    queryResult = QueryDatabaseValue(QUERY_SELECT_MEMBER, idParam, 1, existingId, sizeof(existingId));
    if(queryResult != 1)
    {
        RLOG_WARN("[CLI] HC-05 registration rejected: id=%s, reason=%s", memberId, queryResult == 0 ? "no such member" : "database error");
        return -1;
    }

    pthread_mutex_lock(&pairMutex);
    registerResult = RegisterDevice("CLI", memberId, mac, pin);
    pthread_mutex_unlock(&pairMutex);
    return registerResult;
}

// pairMutex를 잡은 상태에서 호출. 0: 등록 및 연결 완료, -1: 실패 (로그 남김)
static int RegisterDevice(const char *label, const char *memberId, const char *bluetoothMac, const char *pin)
{
    DatabaseValue idParam[1] = {DATABASE_TEXT(memberId)};
    DatabaseValue macParam[1] = {DATABASE_TEXT(bluetoothMac)};
    DatabaseValue registerParams[2];
    char existingMac[BLUETOOTH_MAC_TEXT_SIZE];
    char macOwner[MEM_ID_SIZE + 1];
    uint64_t insertedRows;
    int queryResult;
    int pairResult;
    int pairError;

    if(strnlen(bluetoothMac, BLUETOOTH_MAC_TEXT_SIZE) != BLUETOOTH_MAC_SIZE || pin[0] == '\0')
    {
        RLOG_WARN("[%s] Invalid HC-05 registration data: id=%s", label, memberId);
        return -1;
    }

    queryResult = QueryDatabaseValue(QUERY_SELECT_BLUETOOTH_MAC, idParam, 1, existingMac, sizeof(existingMac));
    if(queryResult != 0)
    {
        RLOG_WARN("[%s] HC-05 registration rejected: id=%s, reason=%s", label, memberId, queryResult > 0 ? "already registered" : "database error");
        return -1;
    }
    // 한 HC-05를 여러 회원에 묶지 않음
    queryResult = QueryDatabaseValue(QUERY_SELECT_BLUETOOTH_MEMBER, macParam, 1, macOwner, sizeof(macOwner));
    if(queryResult != 0)
    {
        RLOG_WARN("[%s] HC-05 registration rejected: id=%s, mac=%s, reason=%s", label, memberId, bluetoothMac, queryResult > 0 ? "MAC registered to another member" : "database error");
        return -1;
    }

    pairResult = PairBluetoothDevice(bluetoothMac, pin, BLUETOOTH_PAIR_TIMEOUT_SECONDS);
    pairError = errno;
    if(pairResult != 0)
    {
        RLOG_WARN("[%s] HC-05 pairing failed: id=%s, mac=%s: %s", label, memberId, bluetoothMac, strerror(pairError));
        return -1;
    }

    registerParams[0] = DATABASE_TEXT(memberId);
    registerParams[1] = DATABASE_TEXT(bluetoothMac);
    if(ExecuteDatabaseQuery(QUERY_INSERT_BLUETOOTH, registerParams, 2, NULL, NULL, &insertedRows) != 0 || insertedRows != 1)
    {
        RLOG_WARN("[%s] HC-05 database registration failed: id=%s", label, memberId);
        return -1;
    }
    RLOG_INFO("[%s] HC-05 registered: id=%s, mac=%s", label, memberId, bluetoothMac);
    if(RPacketBtConnectMember(memberId) != 0)
    {
        return -1;
    }
    return 0;
}

// 회원의 등록 HC-05를 DB에서 찾아 BT 세션으로 연결.
// requestedMac != NULL이면 DB의 ID-MAC 바인딩과 같아야 함. 0: 연결됨, 1: 미등록, -1: 실패
static int ConnectRegisteredDevice(const char *memberId, const char *requestedMac)
{
    DatabaseValue idParam[1];
    char registeredMac[BLUETOOTH_MAC_TEXT_SIZE];
    size_t memberIdLength;
    int queryResult;

    if(memberId == NULL || (memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1)) == 0 || memberIdLength > MEM_ID_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    idParam[0] = DATABASE_TEXT(memberId);
    queryResult = QueryDatabaseValue(QUERY_SELECT_BLUETOOTH_MAC, idParam, 1, registeredMac, sizeof(registeredMac));
    if(queryResult == 0)
    {
        RLOG_INFO("[BT] HC-05 registration required: id=%s", memberId);
        return 1;
    }
    if(queryResult < 0)
    {
        RLOG_WARN("[BT] HC-05 database lookup failed: id=%s", memberId);
        errno = EIO;
        return -1;
    }
    if(requestedMac != NULL && strcasecmp(requestedMac, registeredMac) != 0)
    {
        RLOG_WARN("[BT] ID/MAC binding mismatch: id=%s", memberId);
        errno = EACCES;
        return -1;
    }
    return ConnectDevice(memberId, registeredMac);
}

// 같은 회원이 같은 MAC으로 연결돼 있으면 그대로 0, MAC이 바뀌었으면 이전 연결을 닫고 새로 연결
static int ConnectDevice(const char *memberId, const char *mac)
{
    RMemberType memberType;
    uint8_t rfcommChannel;
    int previousFd;
    int conflict;
    int fd;

    pthread_mutex_lock(&connectMutex);
    conflict = FindBtSessionConflict(memberId, mac, &previousFd);
    if(conflict != 0)
    {
        pthread_mutex_unlock(&connectMutex);
        if(conflict > 0)
        {
            RLOG_INFO("Bluetooth already connected: id=%s, mac=%s, fd=%d", memberId, mac, previousFd);
            return 0;
        }
        RLOG_WARN("Bluetooth MAC already connected to another member: id=%s, mac=%s", memberId, mac);
        errno = EADDRINUSE;
        return -1;
    }
    if(previousFd >= 0)
    {
        RNetClose(previousFd);
    }

    fd = ConnectBluetoothDevice(mac, BLUETOOTH_CONNECT_TIMEOUT_MS, &rfcommChannel);
    if(fd < 0)
    {
        int connectError = errno;

        pthread_mutex_unlock(&connectMutex);
        RLOG_WARN("[BT] HC-05 connection failed: id=%s, mac=%s: %s", memberId, mac, strerror(connectError));
        errno = connectError;
        return -1;
    }
    memberType = RPacketReadMemberType(memberId);
    if(RNetOpenBt(fd, memberId, mac, memberType) != 0)
    {
        int openError = errno;

        pthread_mutex_unlock(&connectMutex);
        RLOG_WARN("[BT] Session not available: id=%s: %s", memberId, strerror(openError));
        errno = openError;
        return -1;
    }
    pthread_mutex_unlock(&connectMutex);
    RLOG_INFO("[BT] HC-05 connected: id=%s, mac=%s, fd=%d, channel=%u", memberId, mac, fd, (unsigned int)rfcommChannel);
    return 0;
}

// 1: 같은 회원·같은 MAC이 이미 연결됨, -1: 다른 회원이 이 MAC 사용 중, 0: 연결해도 됨
// *previousFd: 같은 회원의 연결 fd (1이면 그 연결, 0이면 교체할 이전 연결), 없으면 -1
static int FindBtSessionConflict(const char *memberId, const char *mac, int *previousFd)
{
    RSessionSnapshot snapshots[MAX_SESSION];
    size_t snapshotCount = RSessionGetSnapshots(snapshots);

    *previousFd = -1;
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const RSessionSnapshot *snapshot = &snapshots[index];

        if(snapshot->type != SESSION_BLUETOOTH)
        {
            continue;
        }
        if(strcmp(snapshot->memberId, memberId) == 0)
        {
            *previousFd = snapshot->fd;
            if(strcasecmp(snapshot->address, mac) == 0)
            {
                return 1;
            }
        }
        else if(strcasecmp(snapshot->address, mac) == 0)
        {
            return -1;
        }
    }
    return 0;
}
