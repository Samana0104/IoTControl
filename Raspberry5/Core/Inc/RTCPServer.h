#pragma once

#include "IoTPacket.h"
#include "IoTProtocol.h"
#include "RBluetooth.h"

#include <netinet/in.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stddef.h>

#define BUF_SIZE 100
#define ID_SIZE 10
#define MAX_CLNT 30

typedef struct _TCPServer
{
    /* Settings given to InitServer(). */
    char ip[INET_ADDRSTRLEN];
    int port;

    /* Runtime state; -1 / NULL until OpenServer() succeeds. */
    int socket;
    int databaseInitialized;
    SSL_CTX *tlsContext;

    int signalHandlersInstalled;
    struct sigaction originalInterruptAction;
    struct sigaction originalTerminateAction;
} TCPServer;

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

/* Stores the listen address and prepares client slots and SIGINT/SIGTERM handling.
   Does not touch DB/TLS or the port. ip NULL: all interfaces, port 0: not set yet. */
int InitServer(TCPServer *server, const char *ip, int port);
/* Initializes DB/TLS on first use, then binds server->ip:server->port and listens. */
int OpenServer(TCPServer *server);
/* Accepts clients and runs console commands until quit/exit or SIGINT/SIGTERM. */
int RunServer(TCPServer *server);
/* Disconnects TCP/BT clients, closes the socket, frees TLS and restores signals. */
void CloseServer(TCPServer *server);
/* Same as SIGINT/SIGTERM: the server loop stops and cleans up workers. */
void RequestServerStop(void);

/* Returns 1..65535, or -1 for an invalid port string. */
int ParseServerPort(const char *port);
int IsServerRunning(const TCPServer *server);
int GetServerPort(const TCPServer *server);
int IsServerDatabaseInitialized(const TCPServer *server);

/* snapshots must hold MAX_CLNT * 2 entries. Returns the number filled. */
size_t GetServerClientSnapshots(ServerClientSnapshot *snapshots);
size_t GetServerBluetoothSnapshots(ServerClientSnapshot *snapshots);

/* 0: connected, 1: not registered in DB, -1: failed (errno set). */
int RequestMemberBluetoothConnection(const char *memberId);
