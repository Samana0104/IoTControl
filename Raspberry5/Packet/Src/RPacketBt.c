#include "RPacketBt.h"
#include "IoTPacketCodec.h"
#include "RBluetooth.h"
#include "RDatabase.h"
#include "RLog.h"
#include "RPacket.h"

#include <errno.h>
#include <pthread.h>
#include <sodium.h>
#include <string.h>
#include <strings.h>

#define BLUETOOTH_PAIR_TIMEOUT_SECONDS 30

// BlueZ 에이전트 경로가 하나뿐이므로 페어링은 한 번에 하나씩
static pthread_mutex_t pairMutex = PTHREAD_MUTEX_INITIALIZER;

static int RegisterDevice(RSession *session, const char *memberId, const BluetoothRegisterData *registerData);
static int ConnectRegisteredDevice(const char *memberId, const char *requestedMac);

int RPacketBtRegisterReceive(RSession *session, const uint8_t *payload, size_t length)
{
    BluetoothRegisterData registerData;
    char memberId[MEM_ID_SIZE + 1];
    int registered;

    if(RSessionGetMemberId(session, memberId) != 0 || ReadBluetoothRegisterData(payload, length, &registerData) != 0)
    {
        return -1;
    }
    pthread_mutex_lock(&pairMutex);
    registered = RegisterDevice(session, memberId, &registerData) == 0;
    pthread_mutex_unlock(&pairMutex);
    sodium_memzero(&registerData, sizeof(registerData));

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

    if(RSessionGetType(session) != SESSION_TCP || ReadBluetoothConnectData(payload, length, &request) != 0)
    {
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
    RLOG_INFO("[%s] Bluetooth request: id=%s, result=%s", RSessionGetLabel(session), verifyResult == 1 ? memberId : "-", connected ? "connected" : "failed");
    // BT 세션은 서버 소유라 요청한 클라이언트가 끊겨도 유지됨
    return RPacketSendAck(session, REQ_BT_CONNECT, connected);
}

int RPacketBtConnectMember(const char *memberId)
{
    return ConnectRegisteredDevice(memberId, NULL);
}

// pairMutex를 잡은 상태에서 호출. 0: 등록 및 연결 완료, -1: 실패 (로그 남김)
static int RegisterDevice(RSession *session, const char *memberId, const BluetoothRegisterData *registerData)
{
    BluetoothDeviceRecord existingDevice;
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    char pin[BLUETOOTH_PIN_SIZE + 1];
    size_t macLength = strnlen(registerData->mac, BLUETOOTH_MAC_SIZE);
    size_t pinLength = strnlen(registerData->pin, BLUETOOTH_PIN_SIZE);
    int queryResult;
    int pairResult;
    int pairError;
    int registerResult;

    if(macLength != BLUETOOTH_MAC_SIZE || pinLength == 0)
    {
        RLOG_WARN("[%s] Invalid HC-05 registration data: id=%s", RSessionGetLabel(session), memberId);
        return -1;
    }
    memcpy(bluetoothMac, registerData->mac, macLength);
    bluetoothMac[macLength] = '\0';

    queryResult = GetMemberBluetoothDevice(memberId, strlen(memberId), &existingDevice);
    if(queryResult != 0)
    {
        RLOG_WARN("[%s] HC-05 registration rejected: id=%s, reason=%s", RSessionGetLabel(session), memberId, queryResult > 0 ? "already registered" : "database error");
        return -1;
    }

    memcpy(pin, registerData->pin, pinLength);
    pin[pinLength] = '\0';
    pairResult = PairBluetoothDevice(bluetoothMac, pin, BLUETOOTH_PAIR_TIMEOUT_SECONDS);
    pairError = errno;
    sodium_memzero(pin, sizeof(pin));
    if(pairResult != 0)
    {
        RLOG_WARN("[%s] HC-05 pairing failed: id=%s, mac=%s: %s", RSessionGetLabel(session), memberId, bluetoothMac, strerror(pairError));
        return -1;
    }

    registerResult = RegisterMemberBluetoothDevice(memberId, strlen(memberId), bluetoothMac, macLength);
    if(registerResult != 1)
    {
        RLOG_WARN("[%s] HC-05 database registration failed: id=%s, reason=%s", RSessionGetLabel(session), memberId, registerResult == 0 ? "already registered" : "database error");
        return -1;
    }
    if(RPacketBtConnectMember(memberId) != 0)
    {
        return -1;
    }
    RLOG_INFO("[%s] HC-05 registered: id=%s, mac=%s", RSessionGetLabel(session), memberId, bluetoothMac);
    return 0;
}

// 회원의 등록 HC-05를 DB에서 찾아 BT 세션으로 연결.
// requestedMac != NULL이면 DB의 ID-MAC 바인딩과 같아야 함. 0: 연결됨, 1: 미등록, -1: 실패
static int ConnectRegisteredDevice(const char *memberId, const char *requestedMac)
{
    BluetoothDeviceRecord deviceRecord;
    size_t memberIdLength;
    int queryResult;

    if(memberId == NULL || (memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1)) == 0 || memberIdLength > MEM_ID_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    queryResult = GetMemberBluetoothDevice(memberId, memberIdLength, &deviceRecord);
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
    if(requestedMac != NULL && strcasecmp(requestedMac, deviceRecord.mac) != 0)
    {
        RLOG_WARN("[BT] ID/MAC binding mismatch: id=%s", memberId);
        errno = EACCES;
        return -1;
    }
    return RSessionOpenBt(memberId, deviceRecord.mac) >= 0 ? 0 : -1;
}
