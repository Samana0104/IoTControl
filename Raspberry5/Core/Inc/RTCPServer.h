#pragma once

#include "IoTPacket.h"
#include "IoTProtocol.h"

#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>

typedef struct _TCPServer
{
    /* Settings given to InitServer(). */
    char ip[INET_ADDRSTRLEN];
    int port;

    /* Runtime state; -1 / NULL until OpenServer() succeeds. */
    int socket;
    int databaseInitialized;

    int signalHandlersInstalled;
    struct sigaction originalInterruptAction;
    struct sigaction originalTerminateAction;
} TCPServer;

/* Stores the listen address and prepares client sessions and SIGINT/SIGTERM handling.
   Does not touch the DB or the port. ip NULL: all interfaces, port 0: not set yet. */
int InitServer(TCPServer *server, const char *ip, int port);
/* Reloads ServerConfig.json, initializes the DB on first use, then binds server->ip:server->port and listens. */
int OpenServer(TCPServer *server);
/* Accepts clients and runs console commands until quit/exit or SIGINT/SIGTERM. */
int RunServer(TCPServer *server);
/* Disconnects TCP/BT clients, closes the socket and restores signals. */
void CloseServer(TCPServer *server);
/* Same as SIGINT/SIGTERM: the server loop stops and cleans up workers. */
void RequestServerStop(void);

