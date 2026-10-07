#include "RCmdBt.h"
#include "RPacketBt.h"
#include "RSession.h"

#include <stdio.h>
#include <string.h>

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
    puts("FD  ID       MAC               LINK");
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const RSessionSnapshot *snapshot = &snapshots[index];

        if(snapshot->type != SESSION_BLUETOOTH)
        {
            continue;
        }
        printf("%-3d %-8s %-17s %s\n", snapshot->fd, snapshot->memberId, snapshot->address, snapshot->connected ? "open" : "pending");
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
    else if(!IsServerRunning(server))
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

static const RCommand BT_COMMANDS[] =
{
    {"list", BtList},
    {"connect", BtConnect}
};

void RCmdBt(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "bt", BT_COMMANDS, RCOMMAND_COUNT(BT_COMMANDS));
}
