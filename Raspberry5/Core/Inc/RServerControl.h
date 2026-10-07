#pragma once

#include "RBluetooth.h"
#include "RTCPServer.h"

#include <netinet/in.h>
#include <stddef.h>

/* Server operations used by console commands (TCPServer is in RTCPServer.h). */

typedef struct _ServerClientSnapshot
{
    int index;
    int fd;
    int connected;
    int authenticated;
    int bluetoothFd;
    int bluetoothReceiving;
    char ip[INET_ADDRSTRLEN];
    char memberId[MEM_ID_SIZE + 1];
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
} ServerClientSnapshot;

/* Returns 1..65535, or -1 for an invalid port string. */
int ParseServerPort(const char *port);
/* Same as SIGINT/SIGTERM: the server loop stops and cleans up workers. */
void RequestServerStop(void);
int IsServerRunning(const TCPServer *server);
int GetServerPort(const TCPServer *server);
int IsServerDatabaseInitialized(const TCPServer *server);

/* snapshots must hold MAX_CLNT * 2 entries. Returns the number filled. */
size_t GetServerClientSnapshots(ServerClientSnapshot *snapshots);
size_t GetServerBluetoothSnapshots(ServerClientSnapshot *snapshots);

/* 0: connected, 1: not registered in DB, -1: failed (errno set). */
int RequestMemberBluetoothConnection(const char *memberId);
