#pragma once

#include "IoTProtocol.h"

#include <netinet/in.h>
#include <openssl/ssl.h>
#include <signal.h>

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

/* Stores the listen address and prepares client slots and SIGINT/SIGTERM handling.
   Does not touch DB/TLS or the port. ip NULL: all interfaces, port 0: not set yet. */
int InitServer(TCPServer *server, const char *ip, int port);
/* Initializes DB/TLS on first use, then binds server->ip:server->port and listens. */
int OpenServer(TCPServer *server);
/* Accepts clients and runs console commands until quit/exit or SIGINT/SIGTERM. */
int RunServer(TCPServer *server);
/* Disconnects TCP/BT clients, closes the socket, frees TLS and restores signals. */
void CloseServer(TCPServer *server);
