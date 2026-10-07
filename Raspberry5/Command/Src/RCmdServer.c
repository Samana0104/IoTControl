#include "RCmdServer.h"
#include "RSession.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// 1..65535, 숫자가 아니거나 범위를 벗어나면 -1
static int ParsePort(const char *port)
{
    char *endPointer;
    long parsedPort;

    if(port == NULL || *port == '\0')
    {
        return -1;
    }

    errno = 0;
    parsedPort = strtol(port, &endPointer, 10);
    if(errno != 0 || *endPointer != '\0' || parsedPort < 1 || parsedPort > 65535)
    {
        return -1;
    }

    return (int)parsedPort;
}

// DB/TLS 초기화 후 listen 시작, 포트를 생략하면 main에서 설정한 포트 사용
static void ServerStart(TCPServer *server, const char *args)
{
    if(server->socket >= 0)
    {
        printf("Server already running on %s:%d. Restart the process to change ports.\n", server->ip, server->port);
        return;
    }
    if(*args != '\0')
    {
        int port = ParsePort(args);

        if(!isdigit((unsigned char)*args) || port < 0)
        {
            puts("Usage: server start [port] (1..65535)");
            return;
        }
        server->port = port;
    }
    if(OpenServer(server) != 0)
    {
        puts("Server start failed. Fix the configuration or retry 'server start [port]'.");
    }
}

static void ServerStatus(TCPServer *server, const char *args)
{
    RSessionSnapshot snapshots[MAX_SESSION];
    size_t snapshotCount = RSessionGetSnapshots(snapshots);
    size_t tcpCount = 0;

    (void)args;
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        tcpCount += snapshots[index].type == SESSION_TCP;
    }
    flockfile(stdout);
    if(server->socket >= 0)
    {
        printf("Server: running\nListen: %s:%d (TCP/TLS)\n", server->ip, server->port);
    }
    else
    {
        printf("Server: not started\nListen: none (configured %s:%d, use 'server start [port]')\n", server->ip, server->port);
    }
    printf("Sessions: %zu/%d (TCP %zu, BT %zu)\n", snapshotCount, MAX_SESSION, tcpCount, snapshotCount - tcpCount);
    funlockfile(stdout);
}

// TCP 클라이언트와 BT 장치 세션을 fd 기준으로 함께 표시
static void ServerSessions(TCPServer *server, const char *args)
{
    RSessionSnapshot snapshots[MAX_SESSION];
    size_t snapshotCount = RSessionGetSnapshots(snapshots);

    (void)server;
    (void)args;
    flockfile(stdout);
    puts("FD  TYPE ADDRESS           ID       AUTH");
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const RSessionSnapshot *snapshot = &snapshots[index];
        const char *memberId = snapshot->memberId[0] != '\0' ? snapshot->memberId : "-";

        printf("%-3d %-4s %-17s %-8s %s\n", snapshot->fd, snapshot->type == SESSION_TCP ? "TCP" : "BT", snapshot->address, memberId, snapshot->authenticated ? "yes" : "no");
    }
    if(snapshotCount == 0)
    {
        puts("No sessions.");
    }
    funlockfile(stdout);
}

static const RCommand SERVER_COMMANDS[] =
{
    {"start", ServerStart},
    {"status", ServerStatus},
    {"sessions", ServerSessions}
};

void RCmdServer(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "server", SERVER_COMMANDS, RCOMMAND_COUNT(SERVER_COMMANDS));
}
