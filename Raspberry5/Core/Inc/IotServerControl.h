#pragma once

#include "IotBluetooth.h"
#include "IotSocket.h"

#include <netinet/in.h>
#include <stddef.h>

/* Server operations exposed to the CLI; the state itself stays in IotSocket.c. */
typedef struct _ServerState ServerState;

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
/* Initializes DB/TLS on first use, then binds and listens. Returns 0 on success. */
int StartServerListener(ServerState *server, const char *port);
int IsServerRunning(const ServerState *server);
int GetServerPort(const ServerState *server);
int IsServerDatabaseInitialized(const ServerState *server);

/* snapshots must hold MAX_CLNT * 2 entries. Returns the number filled. */
size_t GetServerClientSnapshots(ServerClientSnapshot *snapshots);
size_t GetServerBluetoothSnapshots(ServerClientSnapshot *snapshots);

/* 0: connected, 1: not registered in DB, -1: failed (errno set). */
int RequestMemberBluetoothConnection(const char *memberId);
