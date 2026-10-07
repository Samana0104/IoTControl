#include "RCmdServer.h"

#include <ctype.h>
#include <stdio.h>

// DB/TLS 초기화 후 포트 listen 시작
static void ServerStart(ServerState *server, const char *args)
{
    const char *port = args;

    if(!isdigit((unsigned char)*port) || ParseServerPort(port) < 0)
    {
        puts("Usage: server start <port> (1..65535)");
    }
    else if(IsServerRunning(server))
    {
        printf("Server already running on port %d. Restart the process to change ports.\n", GetServerPort(server));
    }
    else if(StartServerListener(server, port) != 0)
    {
        puts("Server start failed. Fix the configuration or retry 'server start <port>'.");
    }
}

static void ServerStatus(ServerState *server, const char *args)
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
        printf("Server: running\nListen: 0.0.0.0:%d (TCP/TLS)\n", GetServerPort(server));
    }
    else
    {
        puts("Server: not started\nListen: none (use 'server start <port>')");
    }
    printf("TCP sessions: %zu/%d\nBT sockets: %zu (active receivers: %zu)\n", snapshotCount, MAX_CLNT, bluetoothCount, receiverCount);
    funlockfile(stdout);
}

static void ServerClients(ServerState *server, const char *args)
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

void RCmdServer(ServerState *server, const char *args)
{
    RCommandDispatch(server, args, "server", SERVER_COMMANDS, RCOMMAND_COUNT(SERVER_COMMANDS));
}
