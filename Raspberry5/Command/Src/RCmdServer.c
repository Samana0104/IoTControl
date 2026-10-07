#include "RCmdServer.h"

#include <ctype.h>
#include <stdio.h>

// DB/TLS 초기화 후 listen 시작, 포트를 생략하면 main에서 설정한 포트 사용
static void ServerStart(TCPServer *server, const char *args)
{
    if(IsServerRunning(server))
    {
        printf("Server already running on %s:%d. Restart the process to change ports.\n", server->ip, GetServerPort(server));
        return;
    }
    if(*args != '\0')
    {
        int port = ParseServerPort(args);

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
    ServerClientSnapshot snapshots[MAX_CLNT * 2];
    size_t snapshotCount = GetServerClientSnapshots(snapshots);
    size_t bluetoothCount = GetServerBluetoothSnapshots(snapshots);
    size_t receiverCount = 0;

    (void)args;
    flockfile(stdout);
    for(size_t index = 0; index < bluetoothCount; ++index)
    {
        if(snapshots[index].bluetoothFd >= 0)
        {
            receiverCount += snapshots[index].bluetoothReceiving != 0;
        }
    }
    if(IsServerRunning(server))
    {
        printf("Server: running\nListen: %s:%d (TCP/TLS)\n", server->ip, GetServerPort(server));
    }
    else
    {
        printf("Server: not started\nListen: none (configured %s:%d, use 'server start [port]')\n", server->ip, GetServerPort(server));
    }
    printf("TCP sessions: %zu/%d\nBT sockets: %zu (active receivers: %zu)\n", snapshotCount, MAX_CLNT, bluetoothCount, receiverCount);
    funlockfile(stdout);
}

static void ServerClients(TCPServer *server, const char *args)
{
    ServerClientSnapshot snapshots[MAX_CLNT * 2];
    size_t snapshotCount = GetServerClientSnapshots(snapshots);

    (void)server;
    (void)args;
    flockfile(stdout);
    puts("SLOT FD  IP              ID       AUTH LINK");
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const ServerClientSnapshot *snapshot = &snapshots[index];
        const char *memberId = snapshot->memberId[0] != '\0' ? snapshot->memberId : "-";

        printf("%-4d %-3d %-15s %-8s %-4s %s\n", snapshot->index, snapshot->fd, snapshot->ip, memberId, snapshot->authenticated ? "yes" : "no", snapshot->connected ? "connected" : "closing");
    }
    if(snapshotCount == 0)
    {
        puts("No TCP clients.");
    }
    funlockfile(stdout);
}

static const RCommand SERVER_COMMANDS[] =
{
    {"start", ServerStart},
    {"status", ServerStatus},
    {"clients", ServerClients}
};

void RCmdServer(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "server", SERVER_COMMANDS, RCOMMAND_COUNT(SERVER_COMMANDS));
}
