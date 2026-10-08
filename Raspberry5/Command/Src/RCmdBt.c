#include "RCmdBt.h"
#include "RBluetooth.h"
#include "RDatabaseQuery.h"
#include "RPacketBt.h"
#include "RSession.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BT_SCAN_SECONDS 10
#define BT_SCAN_MAX_DEVICES 32
// bt connectall 한 번에 연결을 시도할 최대 등록 수
#define BT_CONNECT_ALL_MAX MAX_SESSION

// bt connectall: DB에서 읽은 등록 회원 ID 목록
typedef struct _BtRegisteredList
{
    char memberIds[BT_CONNECT_ALL_MAX][MEM_ID_SIZE + 1];
    size_t count;
    int truncated;
} BtRegisteredList;

static int CollectRegisteredMember(const DatabaseRow *row, void *context);

// DB 등록 목록이 아니라 현재 서버가 연결한 BT 세션
static void BtList(TCPServer *server, const char *args)
{
    RSessionSnapshot snapshots[MAX_SESSION];
    size_t snapshotCount = RSessionGetSnapshots(snapshots);
    size_t rowCount = 0;

    (void)server;
    (void)args;
    flockfile(stdout);
    puts("Connected BT sessions (not a list of DB registrations):");
    puts("FD  ID       MAC");
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const RSessionSnapshot *snapshot = &snapshots[index];

        if(snapshot->type != SESSION_BLUETOOTH)
        {
            continue;
        }
        printf("%-3d %-8s %s\n", snapshot->fd, snapshot->memberId, snapshot->address);
        ++rowCount;
    }
    if(rowCount == 0)
    {
        puts("No Bluetooth sessions.");
    }
    funlockfile(stdout);
}

// DB에 등록된 회원 Bluetooth MAC으로 연결 또는 재연결
// 연결 중 로그를 찍는 워커를 기다리므로 stdout 락을 잡지 않는다.
static void BtConnect(TCPServer *server, const char *args)
{
    const char *memberId = args;

    if(*memberId == '\0' || strlen(memberId) > MEM_ID_SIZE || strpbrk(memberId, " \t") != NULL)
    {
        puts("Usage: bt connect <member ID> (1..8 bytes; registered in DB)");
    }
    else if(server->socket < 0)
    {
        puts("Start the server first: server start [port]");
    }
    else
    {
        int connectResult = RPacketBtConnectMember(memberId);

        if(connectResult == 0)
        {
            printf("Bluetooth request: id=%s, result=connected, fd=%d\n", memberId, RSessionFindBtFd(memberId));
        }
        else
        {
            printf("Bluetooth request: id=%s, result=%s\n", memberId, connectResult == 1 ? "not registered" : "failed");
        }
    }
}

// DB bluetooth 테이블에 등록된 모든 회원의 HC-05에 연결 (이미 연결된 기기는 그대로 둠)
// 기기마다 SDP 검색·연결을 기다리므로 등록 수만큼 CLI가 멈출 수 있음
static void BtConnectAll(TCPServer *server, const char *args)
{
    BtRegisteredList registered;
    size_t connectedCount = 0;

    (void)args;
    if(server->socket < 0)
    {
        puts("Start the server first: server start [port]");
        return;
    }
    memset(&registered, 0, sizeof(registered));
    if(ExecuteDatabaseQuery(QUERY_SELECT_BLUETOOTH_ALL, NULL, 0, CollectRegisteredMember, &registered, NULL) != 0)
    {
        puts("Bluetooth registration lookup failed (see log)");
        return;
    }
    if(registered.count == 0)
    {
        puts("No Bluetooth devices registered in DB.");
        return;
    }
    if(registered.truncated)
    {
        printf("Too many registrations; trying the first %zu only.\n", registered.count);
    }

    printf("Connecting %zu registered Bluetooth device(s)...\n", registered.count);
    fflush(stdout);
    for(size_t index = 0; index < registered.count; ++index)
    {
        const char *memberId = registered.memberIds[index];
        int connectResult = RPacketBtConnectMember(memberId);

        if(connectResult == 0)
        {
            ++connectedCount;
            printf("Bluetooth request: id=%s, result=connected, fd=%d\n", memberId, RSessionFindBtFd(memberId));
        }
        else
        {
            printf("Bluetooth request: id=%s, result=%s\n", memberId, connectResult == 1 ? "not registered" : "failed");
        }
        fflush(stdout);
    }
    printf("Bluetooth connect all: %zu/%zu connected\n", connectedCount, registered.count);
}

// 근처 클래식 BT 장치 검색. 이름은 대소문자 무시 부분 일치 (공백 포함 가능)
// 서버 시작 전에도 동작하며, 검색하는 동안 CLI가 멈춤
static void BtScan(TCPServer *server, const char *args)
{
    BluetoothScanDevice devices[BT_SCAN_MAX_DEVICES];
    int deviceCount;

    (void)server;
    printf("Scanning Bluetooth for %d seconds...\n", BT_SCAN_SECONDS);
    fflush(stdout);
    deviceCount = ScanBluetoothDevices(args, BT_SCAN_SECONDS, devices, BT_SCAN_MAX_DEVICES);
    if(deviceCount < 0)
    {
        printf("Bluetooth scan failed: %s\n", strerror(errno));
        return;
    }

    flockfile(stdout);
    puts("MAC               RSSI  PAIRED  NAME");
    for(int index = 0; index < deviceCount; ++index)
    {
        const BluetoothScanDevice *device = &devices[index];

        printf("%-17s %-5d %-7s %s\n", device->mac, device->rssi, device->paired ? "yes" : "no", device->name[0] != '\0' ? device->name : "-");
    }
    if(deviceCount == 0)
    {
        if(*args != '\0')
        {
            printf("No devices matching \"%s\".\n", args);
        }
        else
        {
            puts("No devices found.");
        }
    }
    funlockfile(stdout);
}

// HC-05를 PIN으로 페어링하고 회원의 Bluetooth로 DB에 등록한 뒤 연결
// 페어링·연결 중 로그를 찍으므로 stdout 락을 잡지 않는다.
static void BtPair(TCPServer *server, const char *args)
{
    char memberId[MEM_ID_SIZE + 2];
    char mac[BLUETOOTH_MAC_TEXT_SIZE + 1];
    char pin[BLUETOOTH_PIN_SIZE + 2];
    char extra[2];
    int result;

    if(sscanf(args, "%9s %18s %17s %1s", memberId, mac, pin, extra) != 3 || strlen(memberId) > MEM_ID_SIZE || strlen(mac) != BLUETOOTH_MAC_SIZE || strlen(pin) > BLUETOOTH_PIN_SIZE)
    {
        puts("Usage: bt pair <member ID> <MAC AA:BB:CC:DD:EE:FF> <PIN> (find the MAC with bt scan)");
        memset(pin, 0, sizeof(pin));
        return;
    }
    if(server->socket < 0)
    {
        puts("Start the server first: server start [port]");
        memset(pin, 0, sizeof(pin));
        return;
    }
    // BlueZ가 대문자 주소를 쓰므로 DB에도 대문자로 저장
    for(size_t index = 0; mac[index] != '\0'; ++index)
    {
        mac[index] = (char)toupper((unsigned char)mac[index]);
    }

    printf("Pairing %s for %s...\n", mac, memberId);
    fflush(stdout);
    result = RPacketBtRegisterMember(memberId, mac, pin);
    memset(pin, 0, sizeof(pin));
    if(result == 0)
    {
        printf("Bluetooth registered: id=%s, mac=%s, fd=%d\n", memberId, mac, RSessionFindBtFd(memberId));
    }
    else
    {
        printf("Bluetooth registration failed: id=%s (see log)\n", memberId);
    }
}

// 열 이름 행(values == NULL)은 건너뜀. 연결은 조회가 끝난 뒤에 함 (DB 결과를 오래 잡지 않도록)
static int CollectRegisteredMember(const DatabaseRow *row, void *context)
{
    BtRegisteredList *registered = (BtRegisteredList *)context;
    size_t idLength;

    if(row->values == NULL)
    {
        return 0;
    }
    if(row->values[0] == NULL || (idLength = strlen(row->values[0])) == 0 || idLength > MEM_ID_SIZE)
    {
        return 0;
    }
    if(registered->count == BT_CONNECT_ALL_MAX)
    {
        registered->truncated = 1;
        return 1;
    }
    memcpy(registered->memberIds[registered->count], row->values[0], idLength + 1);
    ++registered->count;
    return 0;
}

static const RCommand BT_COMMANDS[] =
{
    {"list", BtList},
    {"connect", BtConnect},
    {"connectall", BtConnectAll},
    {"scan", BtScan},
    {"pair", BtPair}
};

void RCmdBt(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "bt", BT_COMMANDS, RCOMMAND_COUNT(BT_COMMANDS));
}
