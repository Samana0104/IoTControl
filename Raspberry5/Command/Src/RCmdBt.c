#include "RCmdBt.h"

#include <stdio.h>
#include <string.h>

// DB 등록 목록이 아니라 현재 서버가 보유한 BT 소켓 상태
static void BtList(TCPServer *server, const char *args)
{
    ServerClientSnapshot snapshots[MAX_CLNT * 2];
    size_t snapshotCount = GetServerBluetoothSnapshots(snapshots);
    size_t rowCount = 0;

    (void)server;
    (void)args;
    flockfile(stdout);
    puts("Runtime BT sockets (not a list of DB registrations):");
    puts("SLOT ID       MAC               FD  RX");
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const ServerClientSnapshot *snapshot = &snapshots[index];
        const char *memberId = snapshot->memberId[0] != '\0' ? snapshot->memberId : "-";

        if(snapshot->bluetoothFd < 0)
        {
            continue;
        }
        printf("%-4d %-8s %-17s %-3d %s\n", snapshot->index, memberId, snapshot->bluetoothMac, snapshot->bluetoothFd, snapshot->bluetoothReceiving ? "running" : "stopped");
        ++rowCount;
    }
    if(rowCount == 0)
    {
        puts("No runtime Bluetooth sockets.");
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
    else if(!IsServerRunning(server))
    {
        puts("Start the server first: server start [port]");
    }
    else
    {
        int connectResult = RequestMemberBluetoothConnection(memberId);

        printf("Bluetooth request: id=%s, result=%s\n", memberId, connectResult == 0 ? "connected" : connectResult == 1 ? "not registered" : "failed");
    }
}

static const RCommand BT_COMMANDS[] =
{
    {"list", BtList},
    {"connect", BtConnect}
};

void RCmdBt(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "bt", BT_COMMANDS, RCOMMAND_COUNT(BT_COMMANDS));
}
