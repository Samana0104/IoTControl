#include "RTCPServer.h"
#include "RConfig.h"
#include "RDatabase.h"
#include "RCommand.h"
#include "RPacket.h"
#include "RNetLink.h"
#include "RNetwork.h"
#include "RLog.h"

#include <arpa/inet.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_BACKLOG 5
#define SERVER_POLL_TIMEOUT_MS 500
#define SERVER_CONSOLE_PROMPT "iot-server> "

static volatile sig_atomic_t serverStopRequested;

static void HandleServerStopSignal(int signalNumber);
static void AcceptClient(TCPServer *server);

int InitServer(TCPServer *server, const char *ip, int port)
{
    struct sigaction stopAction = {0};
    struct in_addr address;

    memset(server, 0, sizeof(*server));
    server->socket = -1;
    if(ip == NULL)
    {
        ip = "0.0.0.0";
    }
    if(strlen(ip) >= sizeof(server->ip) || inet_pton(AF_INET, ip, &address) != 1)
    {
        RLOG_ERROR("Invalid server IP: %s", ip);
        return -1;
    }
    if(port < 0 || port > 65535)
    {
        RLOG_ERROR("Invalid port: %d", port);
        return -1;
    }
    strcpy(server->ip, ip);
    server->port = port;

    if(RNetStart(RPacketProcess, RConfigGet()->server.workerCount) != 0)
    {
        RLOG_ERROR("Network start failed: %s", strerror(errno));
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);
    serverStopRequested = 0;
    stopAction.sa_handler = HandleServerStopSignal;
    sigemptyset(&stopAction.sa_mask);
    if(sigaction(SIGINT, &stopAction, &server->originalInterruptAction) != 0)
    {
        RLOG_ERROR("sigaction(SIGINT): %s", strerror(errno));
        return -1;
    }
    if(sigaction(SIGTERM, &stopAction, &server->originalTerminateAction) != 0)
    {
        RLOG_ERROR("sigaction(SIGTERM): %s", strerror(errno));
        sigaction(SIGINT, &server->originalInterruptAction, NULL);
        return -1;
    }
    server->signalHandlersInstalled = 1;
    return 0;
}

int OpenServer(TCPServer *server)
{
    int serverSocket;
    int socketOption = 1;
    struct sockaddr_in serverAddress = {0};

    if(server->socket >= 0)
    {
        RLOG_WARN("Server already running on %s:%d", server->ip, server->port);
        return -1;
    }
    if(server->port <= 0)
    {
        RLOG_WARN("No port set. Use 'server start <port>'.");
        return -1;
    }
    /* Read again on every attempt, so a fixed ServerConfig.json works with 'server start'. */
    if(RConfigLoad() != 0)
    {
        return -1;
    }
    if(!server->databaseInitialized)
    {
        if(InitializeDatabase() != 0)
        {
            return -1;
        }
        server->databaseInitialized = 1;
    }
    if(server->tlsContext == NULL)
    {
        server->tlsContext = RNetLinkCreateTlsContext(&RConfigGet()->tls);
        if(server->tlsContext == NULL)
        {
            return -1;
        }
    }

    serverSocket = socket(PF_INET, SOCK_STREAM, 0);
    if(serverSocket < 0)
    {
        RLOG_ERROR("socket(): %s", strerror(errno));
        return -1;
    }
    if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &socketOption, sizeof(socketOption)) < 0)
    {
        RLOG_ERROR("setsockopt(): %s", strerror(errno));
        close(serverSocket);
        return -1;
    }
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons((uint16_t)server->port);
    inet_pton(AF_INET, server->ip, &serverAddress.sin_addr);
    if(bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        RLOG_ERROR("bind(): %s", strerror(errno));
        close(serverSocket);
        return -1;
    }
    if(listen(serverSocket, LISTEN_BACKLOG) < 0)
    {
        RLOG_ERROR("listen(): %s", strerror(errno));
        close(serverSocket);
        return -1;
    }

    server->socket = serverSocket;
    RLOG_INFO("IoT server started on %s:%d", server->ip, server->port);
    return 0;
}

int RunServer(TCPServer *server)
{
    char line[RCOMMAND_MAX_LINE_SIZE];
    int interactive = isatty(STDIN_FILENO);
    int consoleOpen = 1;
    int result = 0;

    /* No stdio buffer on stdin, so poll() sees every line that fgets() has not read yet. */
    setvbuf(stdin, NULL, _IONBF, 0);
    if(server->socket < 0)
    {
        RLOG_INFO("IoT server console ready. Use 'server start [port]' to start listening.");
    }
    RCommandPrintHelp();
    if(interactive)
    {
        fputs(SERVER_CONSOLE_PROMPT, stdout);
    }
    fflush(stdout);

    while(!serverStopRequested)
    {
        struct pollfd events[2] =
        {
            {.fd = server->socket, .events = POLLIN},
            {.fd = consoleOpen ? STDIN_FILENO : -1, .events = POLLIN}
        };
        int pollResult = poll(events, sizeof(events) / sizeof(events[0]), SERVER_POLL_TIMEOUT_MS);

        if(pollResult < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            RLOG_ERROR("poll(server): %s", strerror(errno));
            result = -1;
            break;
        }

        /* Console: one line per fgets(), handed straight to the command table. */
        if(events[1].revents != 0)
        {
            if(fgets(line, sizeof(line), stdin) == NULL)
            {
                consoleOpen = 0;
                if(server->socket < 0)
                {
                    RLOG_INFO("Server CLI input closed before server start; exiting.");
                    break;
                }
                RLOG_INFO("Server CLI input closed; server continues running.");
            }
            else if(strchr(line, '\n') == NULL && !feof(stdin))
            {
                int character;

                /* Never run a truncated command: drop the rest of the line. */
                while((character = getchar()) != '\n' && character != EOF)
                {
                }
                puts("CLI input is too long; command discarded.");
            }
            else
            {
                line[strcspn(line, "\n")] = '\0';
                RCommandExecute(server, line);
            }
            if(consoleOpen && interactive && !serverStopRequested)
            {
                fputs(SERVER_CONSOLE_PROMPT, stdout);
            }
            fflush(stdout);
        }

        if(events[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            RLOG_ERROR("Listening socket failed");
            result = -1;
            break;
        }
        if(events[0].revents & POLLIN)
        {
            AcceptClient(server);
        }
    }

    return result;
}

void CloseServer(TCPServer *server)
{
    RLOG_INFO("Stopping server; waiting for client and Bluetooth workers...");
    if(server->socket >= 0)
    {
        close(server->socket);
        server->socket = -1;
    }
    RNetStop();
    // 워커 스레드의 DB 연결은 스레드가 끝날 때 닫히고, 메인 스레드(콘솔) 연결은 여기서 닫음
    ResetDatabaseConnection();
    SSL_CTX_free(server->tlsContext);
    server->tlsContext = NULL;
    if(server->signalHandlersInstalled)
    {
        sigaction(SIGINT, &server->originalInterruptAction, NULL);
        sigaction(SIGTERM, &server->originalTerminateAction, NULL);
        server->signalHandlersInstalled = 0;
    }
    RLOG_INFO("IoT server stopped.");
}

void RequestServerStop(void)
{
    serverStopRequested = 1;
}

static void HandleServerStopSignal(int signalNumber)
{
    (void)signalNumber;
    serverStopRequested = 1;
}

/* Accepts one client and hands the socket to RNetwork. */
static void AcceptClient(TCPServer *server)
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);
    int clientSocket;

    clientSocket = accept(server->socket, (struct sockaddr *)&clientAddress, &clientAddressSize);
    if(clientSocket < 0)
    {
        if(errno != EINTR)
        {
            RLOG_ERROR("accept(): %s", strerror(errno));
        }
        return;
    }
    /* RNetwork owns the socket from here, even on failure; a worker runs the TLS handshake. */
    RNetOpenTcp(clientSocket, server->tlsContext, &clientAddress);
}
