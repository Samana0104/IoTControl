#include "RTCPServer.h"
#include "RBluetooth.h"
#include "RDatabase.h"
#include "RCommand.h"
#include "RCtrlCon.h"
#include "RCtrlDht.h"
#include "RCtrlFan.h"
#include "RDatabaseCommand.h"
#include "IoTPacket.h"
#include "IoTPacketCodec.h"
#include "RLog.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define LISTEN_BACKLOG 5
#define SEND_BUFFER_SIZE PACKET_FRAME_SIZE
#define DATA_WAIT_TIMEOUT_MS 5000
#define TLS_HANDSHAKE_TIMEOUT_SECONDS 5
#define BLUETOOTH_CONNECT_TIMEOUT_MS 5000
#define BLUETOOTH_PAIR_TIMEOUT_SECONDS 30
#define SERVER_POLL_TIMEOUT_MS 500
#define SERVER_CONSOLE_INPUT_SIZE (DATABASE_COMMAND_MAX_SQL_SIZE + 8)
#define SERVER_CONSOLE_PROMPT "iot-server> "
#define TLS_CONFIG_FILE "tls_config.txt"
#define TLS_CONFIG_PATH_SIZE 4096
#define TLS_CONFIG_LINE_SIZE (TLS_CONFIG_PATH_SIZE * 2)

typedef struct _TlsConfig
{
    char certificateFile[TLS_CONFIG_PATH_SIZE];
    char privateKeyFile[TLS_CONFIG_PATH_SIZE];
} TlsConfig;

typedef struct _TlsConfigField
{
    const char *key;
    char *value;
    size_t valueSize;
    int seen;
} TlsConfigField;

typedef struct _BluetoothReceiveContext
{
    struct _ClientInfo *owner;
    int bluetoothFd;
    int stopFd;
    char memberId[MEM_ID_SIZE + 1];
    char mac[BLUETOOTH_MAC_TEXT_SIZE];
} BluetoothReceiveContext;

typedef struct _ClientInfo
{
    int index;
    int fd;
    SSL *tls;
    char ip[INET_ADDRSTRLEN];
    char memberId[MEM_ID_SIZE + 1];
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    int bluetoothFd;
    int bluetoothStopFd;
    int bluetoothReceiveStarted;
    int bluetoothReceiving;
    pthread_t bluetoothReceiveThread;
    int authenticated;
    int inUse;
    int threadCount;
    int connected;
    pthread_mutex_t sendMutex;
    pthread_mutex_t tlsMutex;
    pthread_cond_t sendCond;
    uint8_t sendData[SEND_BUFFER_SIZE];
    size_t sendLength;
    int sendPending;
    int sendComplete;
    int sendResult;
} ClientInfo;

/* Console input is line-buffered here and each line is handed to RCommandExecute(). */
typedef struct _ServerConsole
{
    int enabled;
    int interactive;
    int discardingInput;
    size_t inputLength;
    char input[SERVER_CONSOLE_INPUT_SIZE];
} ServerConsole;

typedef enum
{
    PACKET_TRANSPORT_TCP,
    PACKET_TRANSPORT_BLUETOOTH
} PacketTransport;

typedef struct _PacketConnection
{
    PacketTransport transport;
    void *context;
    const char *label;
    int (*receiveAll)(void *context, void *buffer, size_t length);
    int (*sendAll)(void *context, const void *buffer, size_t length);
    int (*waitForData)(void *context, int timeoutMs);
} PacketConnection;

typedef int (*ProcessPacketData)(PacketConnection *connection, const uint8_t *data, size_t length);

typedef struct _PacketHandler
{
    uint16_t cmd;
    size_t minDataLength;
    size_t maxDataLength;
    ProcessPacketData processData;
} PacketHandler;

static ClientInfo clientInfo[MAX_CLNT];
/* BT sessions use only identity/BT lifecycle fields, independently of TCP sessions. */
static ClientInfo bluetoothClients[MAX_CLNT];
static int clientCount;
static int clientCleanupCount;
static pthread_mutex_t clientMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t clientIdleCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t bluetoothPairMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t bluetoothConnectMutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t serverStopRequested;

static void HandleServerStopSignal(int signalNumber);
static void StopServerClients(void);
static void ShowServerConsole(const ServerConsole *console);
static void ReadServerConsole(ServerConsole *console, TCPServer *server);
static void ProcessServerConsoleInput(ServerConsole *console, const char *input, size_t length, TCPServer *server);
static int SetSocketTimeout(int socketFd, int timeoutSeconds);
static int InitializeClients(void);
static int GetClientCount(void);
static ClientInfo *RegisterClient(int clientSocket, SSL *tls, const struct sockaddr_in *clientAddress);
static void UnregisterClientThread(ClientInfo *client);
static void StopClient(ClientInfo *client);
static SSL_CTX *CreateTlsServerContext(void);
static char *TrimTlsConfigText(char *text);
static int ReadTlsConfigLine(FILE *file, char *line, size_t lineSize);
static int LoadTlsConfig(const char *filePath, TlsConfig *config);
static int ReceiveAll(ClientInfo *client, void *buffer, size_t length);
static int WaitForReceiveData(ClientInfo *client, int timeoutMs);
static int SendAll(ClientInfo *client, const void *buffer, size_t length);
static int RequestSend(ClientInfo *client, const void *data, size_t length);
static int ReceiveTcpPacketData(void *context, void *buffer, size_t length);
static int SendTcpPacketData(void *context, const void *buffer, size_t length);
static int WaitForTcpPacketData(void *context, int timeoutMs);
static int WaitForBluetoothEvent(BluetoothReceiveContext *context, short events, int timeoutMs);
static int ReceiveBluetoothPacketData(void *context, void *buffer, size_t length);
static int SendBluetoothPacketData(void *context, const void *buffer, size_t length);
static int WaitForBluetoothPacketData(void *context, int timeoutMs);
static int ValidatePacketPermission(PacketConnection *connection, uint16_t cmd);
static void ReceivePackets(PacketConnection *connection);
static const PacketHandler *FindPacketHandler(uint16_t cmd);
static int GetPacketMemberId(const PacketConnection *connection, char memberId[MEM_ID_SIZE + 1]);
static int SendAck(PacketConnection *connection, uint16_t reqCmd, int succeeded);
static int ProcessDhtData(PacketConnection *connection, const uint8_t *data, size_t length);
static int ProcessFanData(PacketConnection *connection, const uint8_t *data, size_t length);
static int ProcessConData(PacketConnection *connection, const uint8_t *data, size_t length);
static int ProcessMemData(PacketConnection *connection, const uint8_t *data, size_t length);
static int ProcessChatData(PacketConnection *connection, const uint8_t *data, size_t length);
static int ProcessBluetoothRegisterData(PacketConnection *connection, const uint8_t *data, size_t length);
static int ProcessBluetoothConnectData(PacketConnection *connection, const uint8_t *data, size_t length);
static int RequestRegisteredBluetoothConnection(const char *memberId, const char *requestedMac);
static int ConnectMemberBluetoothDevice(ClientInfo *client);
static int ConnectRegisteredBluetoothDevice(ClientInfo *client, const char *requestedMac);
static int StartBluetoothReceive(ClientInfo *client);
static void StopBluetoothReceive(int bluetoothFd, int stopFd, pthread_t receiveThread, int receiveStarted);
static void DisconnectClientBluetooth(ClientInfo *client);
static void *ReceiveBluetooth(void *arg);
static void LogBluetoothData(const BluetoothReceiveContext *context, const uint8_t *data, size_t length);
static void *SendClient(void *arg);
static void *ReceiveClient(void *arg);
static void LogTlsErrors(uint8_t level);

static const PacketHandler PACKET_HANDLERS[] =
{
    {REQ_LOGIN, MEM_DATA_SIZE, MEM_DATA_SIZE, ProcessMemData},
    {REQ_BT_REGISTER, BLUETOOTH_REGISTER_DATA_SIZE, BLUETOOTH_REGISTER_DATA_SIZE, ProcessBluetoothRegisterData},
    {REQ_BT_CONNECT, BLUETOOTH_CONNECT_DATA_SIZE, BLUETOOTH_CONNECT_DATA_SIZE, ProcessBluetoothConnectData},
    {NFY_CHAT, 0, MAX_CHAT_SIZE, ProcessChatData},
    {NFY_DHT, DHT_DATA_SIZE, DHT_DATA_SIZE, ProcessDhtData},
    {NFY_FAN, FAN_DATA_SIZE, FAN_DATA_SIZE, ProcessFanData},
    {NFY_CON, CON_DATA_SIZE, CON_DATA_SIZE, ProcessConData}
};

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

    if(InitializeClients() != 0)
    {
        RLOG_ERROR("client synchronization initialization failed");
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
        server->tlsContext = CreateTlsServerContext();
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
    int result = 0;
    ServerConsole console = {.enabled = 1, .interactive = isatty(STDIN_FILENO)};

    if(server->socket < 0)
    {
        RLOG_INFO("IoT server console ready. Use 'server start [port]' to start listening.");
    }
    ShowServerConsole(&console);
    fflush(stdout);

    while(!serverStopRequested)
    {
        struct pollfd events[2] =
        {
            {.fd = server->socket, .events = POLLIN},
            {.fd = console.enabled ? STDIN_FILENO : -1, .events = POLLIN}
        };
        int pollResult;
        int clientSocket;
        socklen_t clientAddressSize = sizeof(struct sockaddr_in);
        struct sockaddr_in clientAddress;
        ClientInfo *client;
        SSL *tls;
        pthread_t sendThread;
        pthread_t receiveThread;
        int createResult;

        pollResult = poll(events, sizeof(events) / sizeof(events[0]), SERVER_POLL_TIMEOUT_MS);
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
        if(serverStopRequested)
        {
            break;
        }
        if(events[1].revents & (POLLIN | POLLHUP))
        {
            ReadServerConsole(&console, server);
            if(serverStopRequested)
            {
                break;
            }
        }
        if(events[1].revents & (POLLERR | POLLNVAL))
        {
            console.enabled = 0;
            RLOG_WARN("Server CLI input unavailable.");
        }
        if(server->socket < 0 && !console.enabled)
        {
            RLOG_INFO("Server has not been started; exiting.");
            break;
        }
        if(events[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            RLOG_ERROR("Listening socket failed");
            result = -1;
            break;
        }
        if(!(events[0].revents & POLLIN))
        {
            continue;
        }
        clientSocket = accept(server->socket, (struct sockaddr *)&clientAddress, &clientAddressSize);
        if(clientSocket < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            RLOG_ERROR("accept(): %s", strerror(errno));
            continue;
        }

        if(SetSocketTimeout(clientSocket, TLS_HANDSHAKE_TIMEOUT_SECONDS) != 0)
        {
            RLOG_ERROR("setsockopt(client timeout): %s", strerror(errno));
            close(clientSocket);
            continue;
        }

        tls = SSL_new(server->tlsContext);
        if(tls == NULL || SSL_set_fd(tls, clientSocket) != 1 || SSL_accept(tls) != 1)
        {
            RLOG_WARN("TLS handshake failed");
            LogTlsErrors(RLOG_LEVEL_WARN);
            SSL_free(tls);
            close(clientSocket);
            continue;
        }

        if(SetSocketTimeout(clientSocket, 0) != 0)
        {
            RLOG_ERROR("setsockopt(client timeout reset): %s", strerror(errno));
            SSL_shutdown(tls);
            SSL_free(tls);
            close(clientSocket);
            continue;
        }

        client = RegisterClient(clientSocket, tls, &clientAddress);
        if(client == NULL)
        {
            RLOG_WARN("socket full");
            SSL_shutdown(tls);
            SSL_free(tls);
            close(clientSocket);
            continue;
        }

        createResult = pthread_create(&sendThread, NULL, SendClient, client);
        if(createResult != 0)
        {
            RLOG_ERROR("pthread_create(send): %s", strerror(createResult));
            StopClient(client);
            UnregisterClientThread(client);
            UnregisterClientThread(client);
            continue;
        }
        pthread_detach(sendThread);

        createResult = pthread_create(&receiveThread, NULL, ReceiveClient, client);
        if(createResult != 0)
        {
            RLOG_ERROR("pthread_create(receive): %s", strerror(createResult));
            StopClient(client);
            UnregisterClientThread(client);
            continue;
        }
        pthread_detach(receiveThread);

        RLOG_INFO("Client connected: ip=%s, fd=%d, clients=%d", client->ip,client->fd,GetClientCount());
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
    StopServerClients();
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

int IsServerRunning(const TCPServer *server)
{
    return server->socket >= 0;
}

int GetServerPort(const TCPServer *server)
{
    return server->port;
}

int IsServerDatabaseInitialized(const TCPServer *server)
{
    return server->databaseInitialized;
}

static void HandleServerStopSignal(int signalNumber)
{
    (void)signalNumber;
    serverStopRequested = 1;
}

static void ShowServerConsole(const ServerConsole *console)
{
    if(!console->enabled)
    {
        return;
    }
    RCommandPrintHelp();
    if(console->interactive)
    {
        fputs(SERVER_CONSOLE_PROMPT, stdout);
    }
    fflush(stdout);
}

static void ReadServerConsole(ServerConsole *console, TCPServer *server)
{
    char input[SERVER_CONSOLE_INPUT_SIZE];
    ssize_t length = read(STDIN_FILENO, input, sizeof(input));

    if(length > 0)
    {
        ProcessServerConsoleInput(console, input, (size_t)length, server);
        return;
    }
    if(length < 0 && (errno == EINTR || errno == EAGAIN))
    {
        return;
    }
    if(length == 0 && (console->inputLength > 0 || console->discardingInput))
    {
        ProcessServerConsoleInput(console, "\n", 1, server);
        if(serverStopRequested)
        {
            return;
        }
    }
    console->enabled = 0;
    RLOG_INFO("%s", IsServerRunning(server) ? "Server CLI input closed; server continues running." : "Server CLI input closed before server start.");
}

static void ProcessServerConsoleInput(ServerConsole *console, const char *input, size_t length, TCPServer *server)
{
    for(size_t index = 0; index < length; ++index)
    {
        if(input[index] == '\n')
        {
            if(console->discardingInput)
            {
                puts("CLI input is too long; command discarded.");
            }
            else
            {
                console->input[console->inputLength] = '\0';
                RCommandExecute(server, console->input);
            }
            console->inputLength = 0;
            console->discardingInput = 0;
            /* Lines queued after quit/exit are not run. */
            if(serverStopRequested)
            {
                return;
            }
            if(console->interactive)
            {
                fputs(SERVER_CONSOLE_PROMPT, stdout);
            }
            fflush(stdout);
        }
        else if(!console->discardingInput)
        {
            if(input[index] == '\0' || console->inputLength == sizeof(console->input) - 1)
            {
                /* Discard the whole line; never execute a truncated command. */
                console->discardingInput = 1;
            }
            else
            {
                console->input[console->inputLength++] = input[index];
            }
        }
    }
}

static void StopServerClients(void)
{
    pthread_mutex_lock(&clientMutex);
    for(int index = 0; index < MAX_CLNT; ++index)
    {
        if(clientInfo[index].inUse)
        {
            StopClient(&clientInfo[index]);
        }
    }
    while(clientCount > 0 || clientCleanupCount > 0)
    {
        pthread_cond_wait(&clientIdleCond, &clientMutex);
    }
    pthread_mutex_unlock(&clientMutex);

    /* TCP request workers have finished; no new external BT requests can arrive. */
    pthread_mutex_lock(&bluetoothConnectMutex);
    for(int index = 0; index < MAX_CLNT; ++index)
    {
        if(bluetoothClients[index].inUse)
        {
            DisconnectClientBluetooth(&bluetoothClients[index]);
            pthread_mutex_lock(&clientMutex);
            bluetoothClients[index].inUse = 0;
            pthread_mutex_unlock(&clientMutex);
        }
    }
    pthread_mutex_unlock(&bluetoothConnectMutex);
}

size_t GetServerClientSnapshots(ServerClientSnapshot *snapshots)
{
    size_t snapshotCount = 0;

    pthread_mutex_lock(&clientMutex);
    for(int index = 0; index < MAX_CLNT; ++index)
    {
        ClientInfo *client = &clientInfo[index];
        ServerClientSnapshot *snapshot;

        if(!client->inUse)
        {
            continue;
        }
        snapshot = &snapshots[snapshotCount++];
        snapshot->index = client->index;
        snapshot->fd = client->fd;
        snapshot->authenticated = client->authenticated;
        snapshot->bluetoothFd = client->bluetoothFd;
        snapshot->bluetoothReceiving = client->bluetoothReceiving;
        memcpy(snapshot->ip, client->ip, sizeof(snapshot->ip));
        memcpy(snapshot->memberId, client->memberId, sizeof(snapshot->memberId));
        memcpy(snapshot->bluetoothMac, client->bluetoothMac, sizeof(snapshot->bluetoothMac));
        pthread_mutex_lock(&client->sendMutex);
        snapshot->connected = client->connected;
        pthread_mutex_unlock(&client->sendMutex);
    }
    pthread_mutex_unlock(&clientMutex);
    return snapshotCount;
}

size_t GetServerBluetoothSnapshots(ServerClientSnapshot *snapshots)
{
    size_t snapshotCount = 0;

    pthread_mutex_lock(&clientMutex);
    for(int index = 0; index < MAX_CLNT * 2; ++index)
    {
        const ClientInfo *client = index < MAX_CLNT ? &clientInfo[index] : &bluetoothClients[index - MAX_CLNT];
        ServerClientSnapshot *snapshot;

        if(!client->inUse || client->bluetoothFd < 0)
        {
            continue;
        }
        snapshot = &snapshots[snapshotCount++];
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->index = client->index;
        snapshot->bluetoothFd = client->bluetoothFd;
        snapshot->bluetoothReceiving = client->bluetoothReceiving;
        memcpy(snapshot->memberId, client->memberId, sizeof(snapshot->memberId));
        memcpy(snapshot->bluetoothMac, client->bluetoothMac, sizeof(snapshot->bluetoothMac));
    }
    pthread_mutex_unlock(&clientMutex);
    return snapshotCount;
}

int ParseServerPort(const char *port)
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

static int SetSocketTimeout(int socketFd, int timeoutSeconds)
{
    struct timeval timeout;

    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;
    return setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 && setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 ? 0 : -1;
}

static int InitializeClients(void)
{
    int i;

    memset(clientInfo, 0, sizeof(clientInfo));
    memset(bluetoothClients, 0, sizeof(bluetoothClients));
    for(i = 0; i < MAX_CLNT; i++)
    {
        bluetoothClients[i].index = i;
        bluetoothClients[i].fd = -1;
        bluetoothClients[i].bluetoothFd = -1;
        bluetoothClients[i].bluetoothStopFd = -1;
        clientInfo[i].index = i;
        clientInfo[i].fd = -1;
        clientInfo[i].bluetoothFd = -1;
        clientInfo[i].bluetoothStopFd = -1;

        if(pthread_mutex_init(&clientInfo[i].sendMutex, NULL) != 0)
        {
            return -1;
        }

        if(pthread_mutex_init(&clientInfo[i].tlsMutex, NULL) != 0)
        {
            return -1;
        }

        if(pthread_cond_init(&clientInfo[i].sendCond, NULL) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int GetClientCount(void)
{
    int count;

    pthread_mutex_lock(&clientMutex);
    count = clientCount;
    pthread_mutex_unlock(&clientMutex);

    return count;
}

static ClientInfo *RegisterClient(int clientSocket, SSL *tls, const struct sockaddr_in *clientAddress)
{
    ClientInfo *client = NULL;
    int i;

    pthread_mutex_lock(&clientMutex);
    for(i = 0; i < MAX_CLNT; i++)
    {
        if(!clientInfo[i].inUse)
        {
            client = &clientInfo[i];
            client->fd = clientSocket;
            client->tls = tls;
            client->inUse = 1;
            client->threadCount = 2;
            client->connected = 1;
            client->sendLength = 0;
            client->sendPending = 0;
            client->sendComplete = 0;
            client->sendResult = 0;
            client->authenticated = 0;
            client->bluetoothFd = -1;
            client->bluetoothStopFd = -1;
            client->bluetoothReceiveStarted = 0;
            client->bluetoothReceiving = 0;
            memset(client->memberId, 0, sizeof(client->memberId));
            memset(client->bluetoothMac, 0, sizeof(client->bluetoothMac));
            inet_ntop(AF_INET,&clientAddress->sin_addr,client->ip,sizeof(client->ip));
            clientCount++;
            break;
        }
    }
    pthread_mutex_unlock(&clientMutex);

    return client;
}

static void UnregisterClientThread(ClientInfo *client)
{
    int socketToClose = -1;
    int bluetoothToClose = -1;
    int bluetoothStopFd = -1;
    int bluetoothReceiveStarted = 0;
    pthread_t bluetoothReceiveThread = 0;
    SSL *tlsToFree = NULL;
    int remainingClients = 0;
    char clientIp[INET_ADDRSTRLEN];

    pthread_mutex_lock(&clientMutex);
    client->threadCount--;
    if(client->threadCount == 0)
    {
        socketToClose = client->fd;
        bluetoothToClose = client->bluetoothFd;
        bluetoothStopFd = client->bluetoothStopFd;
        bluetoothReceiveStarted = client->bluetoothReceiveStarted;
        bluetoothReceiveThread = client->bluetoothReceiveThread;
        tlsToFree = client->tls;
        client->fd = -1;
        client->bluetoothFd = -1;
        client->bluetoothStopFd = -1;
        client->bluetoothReceiveStarted = 0;
        client->bluetoothReceiving = 0;
        client->tls = NULL;
        client->inUse = 0;
        clientCount--;
        remainingClients = clientCount;
        ++clientCleanupCount;
        memcpy(clientIp, client->ip, sizeof(clientIp));
    }
    pthread_mutex_unlock(&clientMutex);

    if(socketToClose >= 0)
    {
        StopBluetoothReceive(bluetoothToClose, bluetoothStopFd, bluetoothReceiveThread, bluetoothReceiveStarted);
        SSL_free(tlsToFree);
        close(socketToClose);
        RLOG_INFO("Client disconnected: ip=%s, clients=%d", clientIp, remainingClients);
        pthread_mutex_lock(&clientMutex);
        --clientCleanupCount;
        pthread_cond_broadcast(&clientIdleCond);
        pthread_mutex_unlock(&clientMutex);
    }
}

static void StopClient(ClientInfo *client)
{
    int shouldShutdown = 0;

    pthread_mutex_lock(&client->sendMutex);
    if(client->connected)
    {
        client->connected = 0;
        shouldShutdown = 1;
    }
    pthread_cond_broadcast(&client->sendCond);
    pthread_mutex_unlock(&client->sendMutex);

    if(shouldShutdown)
    {
        shutdown(client->fd, SHUT_RDWR);
    }
}

static char *TrimTlsConfigText(char *text)
{
    size_t length;

    while(isspace((unsigned char)*text))
    {
        ++text;
    }
    length = strlen(text);
    while(length > 0 && isspace((unsigned char)text[length - 1]))
    {
        text[--length] = '\0';
    }
    return text;
}

static int ReadTlsConfigLine(FILE *file, char *line, size_t lineSize)
{
    size_t length = 0;
    int character;

    while((character = fgetc(file)) != EOF && character != '\n')
    {
        if(character == '\0' || length + 1 >= lineSize)
        {
            return -1;
        }
        line[length++] = (char)character;
    }
    line[length] = '\0';
    if(ferror(file))
    {
        return -1;
    }
    return character == EOF && length == 0 ? 0 : 1;
}

static int LoadTlsConfig(const char *filePath, TlsConfig *config)
{
    char line[TLS_CONFIG_LINE_SIZE];
    TlsConfigField fields[] =
    {
        {"IOT_TLS_CERT_FILE", config->certificateFile, sizeof(config->certificateFile), 0},
        {"IOT_TLS_KEY_FILE", config->privateKeyFile, sizeof(config->privateKeyFile), 0}
    };
    struct stat fileStatus;
    int fileDescriptor = -1;
    FILE *file = NULL;
    int readResult;
    int result = -1;
    size_t lineNumber = 0;

    memset(config, 0, sizeof(*config));
    fileDescriptor = open(filePath, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(fileDescriptor < 0)
    {
        RLOG_ERROR("Cannot open TLS config file '%s': %s", filePath, strerror(errno));
        goto cleanup;
    }
    if(fstat(fileDescriptor, &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode))
    {
        RLOG_ERROR("TLS config must be a regular file");
        goto cleanup;
    }
    if(fileStatus.st_mode & (S_IRWXG | S_IRWXO))
    {
        RLOG_ERROR("TLS config permissions are too open. Run: chmod 600 %s", filePath);
        goto cleanup;
    }
    file = fdopen(fileDescriptor, "r");
    if(file == NULL)
    {
        RLOG_ERROR("Cannot read TLS config file");
        goto cleanup;
    }
    fileDescriptor = -1;
    while((readResult = ReadTlsConfigLine(file, line, sizeof(line))) > 0)
    {
        char *key = line;
        char *value;
        char *separator;
        size_t valueLength;
        TlsConfigField *field = NULL;

        ++lineNumber;
        if(lineNumber == 1 && strncmp(key, "\xEF\xBB\xBF", 3) == 0)
        {
            key += 3;
        }
        key = TrimTlsConfigText(key);
        if(*key == '\0' || *key == '#')
        {
            continue;
        }
        separator = strchr(key, '=');
        if(separator == NULL)
        {
            RLOG_ERROR("Expected KEY=value in TLS config at line %zu", lineNumber);
            goto cleanup;
        }
        *separator = '\0';
        key = TrimTlsConfigText(key);
        value = TrimTlsConfigText(separator + 1);
        valueLength = strlen(value);
        if(*value == '\'' || *value == '"')
        {
            if(valueLength < 2 || value[valueLength - 1] != *value)
            {
                RLOG_ERROR("Unmatched quotes in TLS config at line %zu", lineNumber);
                goto cleanup;
            }
            value[valueLength - 1] = '\0';
            ++value;
            valueLength -= 2;
        }
        for(size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index)
        {
            if(strcmp(key, fields[index].key) == 0)
            {
                field = &fields[index];
                break;
            }
        }
        if(field == NULL || field->seen)
        {
            RLOG_ERROR("Unknown or duplicate TLS config key at line %zu", lineNumber);
            goto cleanup;
        }
        if(valueLength == 0 || valueLength >= field->valueSize)
        {
            RLOG_ERROR("Invalid value length for %s at line %zu", field->key, lineNumber);
            goto cleanup;
        }
        memcpy(field->value, value, valueLength + 1);
        field->seen = 1;
    }
    if(readResult < 0)
    {
        RLOG_ERROR("Unreadable, binary or oversized TLS config line at line %zu", lineNumber + 1);
        goto cleanup;
    }
    for(size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index)
    {
        if(!fields[index].seen)
        {
            RLOG_ERROR("Missing %s in TLS config", fields[index].key);
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    if(file != NULL)
    {
        fclose(file);
    }
    if(fileDescriptor >= 0)
    {
        close(fileDescriptor);
    }
    if(result != 0)
    {
        memset(config, 0, sizeof(*config));
    }
    return result;
}

static SSL_CTX *CreateTlsServerContext(void)
{
    TlsConfig config;
    SSL_CTX *tlsContext;

    if(LoadTlsConfig(TLS_CONFIG_FILE, &config) != 0)
    {
        return NULL;
    }

    tlsContext = SSL_CTX_new(TLS_server_method());
    if(tlsContext == NULL)
    {
        RLOG_ERROR("SSL_CTX_new failed");
        LogTlsErrors(RLOG_LEVEL_ERROR);
        return NULL;
    }

    if(SSL_CTX_set_min_proto_version(tlsContext, TLS1_2_VERSION) != 1 || SSL_CTX_use_certificate_chain_file(tlsContext, config.certificateFile) != 1 || SSL_CTX_use_PrivateKey_file(tlsContext, config.privateKeyFile, SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(tlsContext) != 1)
    {
        RLOG_ERROR("TLS certificate initialization failed");
        LogTlsErrors(RLOG_LEVEL_ERROR);
        SSL_CTX_free(tlsContext);
        return NULL;
    }

    SSL_CTX_set_options(tlsContext, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_mode(tlsContext, SSL_MODE_AUTO_RETRY);
    return tlsContext;
}

static int ReceiveAll(ClientInfo *client, void *buffer, size_t length)
{
    uint8_t *current = (uint8_t *)buffer;
    size_t receivedLength = 0;

    while(receivedLength < length)
    {
        int result;
        int tlsError;

        pthread_mutex_lock(&client->tlsMutex);
        ERR_clear_error();
        result = SSL_read(client->tls, current + receivedLength, (int)(length - receivedLength));
        tlsError = result > 0 ? SSL_ERROR_NONE : SSL_get_error(client->tls, result);
        pthread_mutex_unlock(&client->tlsMutex);

        if(result == 0)
        {
            return -1;
        }
        if(result < 0)
        {
            if(tlsError == SSL_ERROR_WANT_READ || tlsError == SSL_ERROR_WANT_WRITE || (tlsError == SSL_ERROR_SYSCALL && errno == EINTR))
            {
                continue;
            }
            return -1;
        }

        receivedLength += (size_t)result;
    }

    return 0;
}

static int WaitForReceiveData(ClientInfo *client, int timeoutMs)
{
    struct pollfd socketEvent;
    int pendingData;

    pthread_mutex_lock(&client->tlsMutex);
    pendingData = SSL_pending(client->tls);
    pthread_mutex_unlock(&client->tlsMutex);
    if(pendingData > 0)
    {
        return 1;
    }

    socketEvent.fd = client->fd;
    socketEvent.events = POLLIN;
    socketEvent.revents = 0;

    while(1)
    {
        int result = poll(&socketEvent, 1, timeoutMs);

        if(result > 0)
        {
            if(socketEvent.revents & POLLIN)
            {
                return 1;
            }

            return -1;
        }

        if(result == 0)
        {
            return 0;
        }

        if(errno != EINTR)
        {
            return -1;
        }
    }
}

static int SendAll(ClientInfo *client, const void *buffer, size_t length)
{
    const uint8_t *current = (const uint8_t *)buffer;
    size_t sentLength = 0;

    while(sentLength < length)
    {
        int result;
        int tlsError;

        pthread_mutex_lock(&client->tlsMutex);
        ERR_clear_error();
        result = SSL_write(client->tls, current + sentLength, (int)(length - sentLength));
        tlsError = result > 0 ? SSL_ERROR_NONE : SSL_get_error(client->tls, result);
        pthread_mutex_unlock(&client->tlsMutex);

        if(result == 0)
        {
            return -1;
        }
        if(result < 0)
        {
            if(tlsError == SSL_ERROR_WANT_READ || tlsError == SSL_ERROR_WANT_WRITE || (tlsError == SSL_ERROR_SYSCALL && errno == EINTR))
            {
                continue;
            }
            return -1;
        }

        sentLength += (size_t)result;
    }

    return 0;
}

static int RequestSend(ClientInfo *client, const void *data, size_t length)
{
    int result;

    if(length > sizeof(client->sendData))
    {
        return -1;
    }

    pthread_mutex_lock(&client->sendMutex);
    while(client->sendPending && client->connected)
    {
        pthread_cond_wait(&client->sendCond, &client->sendMutex);
    }

    if(!client->connected)
    {
        pthread_mutex_unlock(&client->sendMutex);
        return -1;
    }

    memcpy(client->sendData, data, length);
    client->sendLength = length;
    client->sendPending = 1;
    client->sendComplete = 0;
    pthread_cond_broadcast(&client->sendCond);

    while(!client->sendComplete && client->connected)
    {
        pthread_cond_wait(&client->sendCond, &client->sendMutex);
    }

    result = client->sendComplete ? client->sendResult : -1;
    pthread_mutex_unlock(&client->sendMutex);

    return result;
}

static int ReceiveTcpPacketData(void *context, void *buffer, size_t length)
{
    return ReceiveAll((ClientInfo *)context, buffer, length);
}

static int SendTcpPacketData(void *context, const void *buffer, size_t length)
{
    return RequestSend((ClientInfo *)context, buffer, length);
}

static int WaitForTcpPacketData(void *context, int timeoutMs)
{
    return WaitForReceiveData((ClientInfo *)context, timeoutMs);
}

static int WaitForBluetoothEvent(BluetoothReceiveContext *context, short events, int timeoutMs)
{
    struct pollfd pollEvents[2] =
    {
        {.fd = context->bluetoothFd, .events = events},
        {.fd = context->stopFd, .events = POLLIN}
    };

    while(1)
    {
        int result = poll(pollEvents, sizeof(pollEvents) / sizeof(pollEvents[0]), timeoutMs);

        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if(result == 0)
        {
            return 0;
        }
        if(pollEvents[1].revents != 0)
        {
            errno = ECANCELED;
            return -1;
        }
        if(pollEvents[0].revents & events)
        {
            return 1;
        }
        if(pollEvents[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            errno = ECONNRESET;
            return -1;
        }
    }
}

static int ReceiveBluetoothPacketData(void *context, void *buffer, size_t length)
{
    BluetoothReceiveContext *bluetooth = (BluetoothReceiveContext *)context;
    uint8_t *current = (uint8_t *)buffer;
    size_t receivedLength = 0;

    while(receivedLength < length)
    {
        ssize_t result;

        if(WaitForBluetoothEvent(bluetooth, POLLIN, -1) < 0)
        {
            return -1;
        }
        result = recv(bluetooth->bluetoothFd, current + receivedLength, length - receivedLength, MSG_DONTWAIT);
        if(result > 0)
        {
            LogBluetoothData(bluetooth, current + receivedLength, (size_t)result);
            receivedLength += (size_t)result;
            continue;
        }
        if(result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        {
            continue;
        }
        if(result == 0)
        {
            errno = ECONNRESET;
        }
        return -1;
    }
    return 0;
}

static int SendBluetoothPacketData(void *context, const void *buffer, size_t length)
{
    BluetoothReceiveContext *bluetooth = (BluetoothReceiveContext *)context;
    const uint8_t *current = (const uint8_t *)buffer;
    size_t sentLength = 0;

    while(sentLength < length)
    {
        ssize_t result;

        if(WaitForBluetoothEvent(bluetooth, POLLOUT, -1) < 0)
        {
            return -1;
        }
        result = send(bluetooth->bluetoothFd, current + sentLength, length - sentLength, MSG_DONTWAIT | MSG_NOSIGNAL);
        if(result > 0)
        {
            sentLength += (size_t)result;
            continue;
        }
        if(result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        {
            continue;
        }
        if(result == 0)
        {
            errno = ECONNRESET;
        }
        return -1;
    }
    return 0;
}

static int WaitForBluetoothPacketData(void *context, int timeoutMs)
{
    return WaitForBluetoothEvent((BluetoothReceiveContext *)context, POLLIN, timeoutMs);
}

static const PacketHandler *FindPacketHandler(uint16_t cmd)
{
    size_t i;

    for(i = 0; i < sizeof(PACKET_HANDLERS) / sizeof(PACKET_HANDLERS[0]); i++)
    {
        if(PACKET_HANDLERS[i].cmd == cmd)
        {
            return &PACKET_HANDLERS[i];
        }
    }

    return NULL;
}

static int GetPacketMemberId(const PacketConnection *connection, char memberId[MEM_ID_SIZE + 1])
{
    if(connection == NULL || connection->context == NULL)
    {
        return -1;
    }
    if(connection->transport == PACKET_TRANSPORT_BLUETOOTH)
    {
        const BluetoothReceiveContext *bluetooth = connection->context;

        memcpy(memberId, bluetooth->memberId, MEM_ID_SIZE + 1);
    }
    else if(connection->transport == PACKET_TRANSPORT_TCP)
    {
        const ClientInfo *client = connection->context;

        pthread_mutex_lock(&clientMutex);
        if(!client->authenticated)
        {
            pthread_mutex_unlock(&clientMutex);
            return -1;
        }
        memcpy(memberId, client->memberId, MEM_ID_SIZE + 1);
        pthread_mutex_unlock(&clientMutex);
    }
    else
    {
        return -1;
    }
    memberId[MEM_ID_SIZE] = '\0';
    return memberId[0] != '\0' ? 0 : -1;
}

static int SendAck(PacketConnection *connection, uint16_t reqCmd, int succeeded)
{
    uint8_t frame[HEADER_SIZE + RESULT_DATA_SIZE];

    MakeAckPacket(frame, sizeof(frame), reqCmd, succeeded ? RESULT_SUCCESS : RESULT_FAIL);
    return connection->sendAll(connection->context, frame, sizeof(frame));
}

static int ProcessDhtData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    DhtData dhtData;
    char memberId[MEM_ID_SIZE + 1];

    if(data == NULL || ReadDhtData(data, length, &dhtData) != 0 || GetPacketMemberId(connection, memberId) != 0)
    {
        return -1;
    }
    /* DB errors must not tear down an otherwise valid TCP/BT connection. */
    RCtrlDhtReceive(connection->label, memberId, &dhtData);
    return 0;
}

static int ProcessFanData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    FanData fanData;

    if(connection == NULL || data == NULL || ReadFanData(data, length, &fanData) != 0)
    {
        return -1;
    }
    RCtrlFanReceive(connection->label, &fanData);
    return 0;
}

static int ProcessConData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    ConData conData;

    if(connection == NULL || data == NULL || ReadConData(data, length, &conData) != 0)
    {
        return -1;
    }
    RCtrlConReceive(connection->label, &conData);
    return 0;
}

static int ProcessMemData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    ClientInfo *client = (ClientInfo *)connection->context;
    MemData memData;
    size_t memberIdLength;
    size_t passwordLength;
    int verifyResult;

    if(ReadMemData(data, length, &memData) != 0)
    {
        return -1;
    }
    memberIdLength = strnlen(memData.id, MEM_ID_SIZE);
    passwordLength = strnlen(memData.pw, MEM_PW_SIZE);
    verifyResult = VerifyMember(memData.id, memberIdLength, memData.pw, passwordLength);

    if(verifyResult == 1)
    {
        pthread_mutex_lock(&clientMutex);
        memcpy(client->memberId, memData.id, memberIdLength);
        client->memberId[memberIdLength] = '\0';
        client->authenticated = 1;
        pthread_mutex_unlock(&clientMutex);
    }

    sodium_memzero(&memData, sizeof(memData));
    if(verifyResult != 1)
    {
        DisconnectClientBluetooth(client);
        pthread_mutex_lock(&clientMutex);
        client->memberId[0] = '\0';
        client->authenticated = 0;
        pthread_mutex_unlock(&clientMutex);
        RLOG_WARN("[%s] Member authentication failed", client->ip);
        SendAck(connection, REQ_LOGIN, 0);
        return -1;
    }

    RLOG_INFO("[%s] Member authenticated: id=%s", client->ip, client->memberId);
    return SendAck(connection, REQ_LOGIN, 1);
}

static int ProcessBluetoothRegisterData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    ClientInfo *client = (ClientInfo *)connection->context;
    BluetoothRegisterData registerData;
    BluetoothDeviceRecord existingDevice;
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    char pin[BLUETOOTH_PIN_SIZE + 1];
    size_t macLength;
    size_t pinLength;
    int queryResult;
    int registerResult;
    int processResult = -1;

    if(ReadBluetoothRegisterData(data, length, &registerData) != 0)
    {
        return -1;
    }
    pthread_mutex_lock(&bluetoothPairMutex);
    macLength = strnlen(registerData.mac, BLUETOOTH_MAC_SIZE);
    pinLength = strnlen(registerData.pin, BLUETOOTH_PIN_SIZE);
    if(macLength != BLUETOOTH_MAC_SIZE || pinLength == 0)
    {
        RLOG_WARN("[%s] Invalid HC-05 registration data: id=%s", client->ip, client->memberId);
        goto cleanup;
    }

    memcpy(bluetoothMac, registerData.mac, macLength);
    bluetoothMac[macLength] = '\0';
    memcpy(pin, registerData.pin, pinLength);
    pin[pinLength] = '\0';

    queryResult = GetMemberBluetoothDevice(client->memberId, strlen(client->memberId), &existingDevice);
    if(queryResult != 0)
    {
        RLOG_WARN("[%s] HC-05 registration rejected: id=%s, reason=%s", client->ip, client->memberId, queryResult > 0 ? "already registered" : "database error");
        goto cleanup;
    }

    if(PairBluetoothDevice(bluetoothMac, pin, BLUETOOTH_PAIR_TIMEOUT_SECONDS) != 0)
    {
        RLOG_WARN("[%s] HC-05 pairing failed: id=%s, mac=%s: %s", client->ip, client->memberId, bluetoothMac, strerror(errno));
        goto cleanup;
    }

    registerResult = RegisterMemberBluetoothDevice(client->memberId, strlen(client->memberId), bluetoothMac, macLength);
    if(registerResult != 1)
    {
        RLOG_WARN("[%s] HC-05 database registration failed: id=%s, reason=%s", client->ip, client->memberId, registerResult == 0 ? "already registered" : "database error");
        goto cleanup;
    }

    if(RequestMemberBluetoothConnection(client->memberId) != 0)
    {
        goto cleanup;
    }

    RLOG_INFO("[%s] HC-05 registered: id=%s, mac=%s", client->ip, client->memberId, bluetoothMac);
    processResult = 0;

cleanup:
    pthread_mutex_unlock(&bluetoothPairMutex);
    sodium_memzero(pin, sizeof(pin));
    sodium_memzero(&registerData, sizeof(registerData));
    if(SendAck(connection, REQ_BT_REGISTER, processResult == 0) != 0)
    {
        processResult = -1;
    }
    return processResult;
}

static int ProcessBluetoothConnectData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    BluetoothConnectData request;
    int connected = 0;
    char memberId[MEM_ID_SIZE + 1];
    char requestedMac[BLUETOOTH_MAC_TEXT_SIZE];
    size_t memberIdLength;
    size_t passwordLength;
    int verifyResult;

    if(connection->transport != PACKET_TRANSPORT_TCP || data == NULL || ReadBluetoothConnectData(data, length, &request) != 0)
    {
        return -1;
    }
    memberIdLength = strnlen(request.id, sizeof(request.id));
    passwordLength = strnlen(request.pw, sizeof(request.pw));
    memcpy(memberId, request.id, memberIdLength);
    memberId[memberIdLength] = '\0';
    memcpy(requestedMac, request.mac, sizeof(request.mac));
    requestedMac[sizeof(request.mac)] = '\0';
    verifyResult = memberIdLength > 0 && passwordLength > 0 && strnlen(request.mac, sizeof(request.mac)) == sizeof(request.mac) ? VerifyMember(request.id, memberIdLength, request.pw, passwordLength) : 0;
    sodium_memzero(&request, sizeof(request));

    if(verifyResult == 1 && RequestRegisteredBluetoothConnection(memberId, requestedMac) == 0)
    {
        connected = 1;
    }
    RLOG_INFO("[%s] Bluetooth request: id=%s, result=%s", connection->label, verifyResult == 1 ? memberId : "-", connected ? "connected" : "failed");
    /* BT is server-owned already; losing this result recipient must not close it. */
    return SendAck(connection, REQ_BT_CONNECT, connected);
}

int RequestMemberBluetoothConnection(const char *memberId)
{
    return RequestRegisteredBluetoothConnection(memberId, NULL);
}

static int RequestRegisteredBluetoothConnection(const char *memberId, const char *requestedMac)
{
    ClientInfo *bluetoothClient = NULL;
    ClientInfo *availableClient = NULL;
    size_t memberIdLength;
    int result;
    int keepSession;
    int connectError;

    if(memberId == NULL || (memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1)) == 0 || memberIdLength > MEM_ID_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    /* Serialize lookup/connect/replace: CLI, registration and external requests. */
    pthread_mutex_lock(&bluetoothConnectMutex);
    if(serverStopRequested)
    {
        pthread_mutex_unlock(&bluetoothConnectMutex);
        errno = ECANCELED;
        return -1;
    }
    pthread_mutex_lock(&clientMutex);
    for(int index = 0; index < MAX_CLNT; ++index)
    {
        ClientInfo *candidate = &bluetoothClients[index];

        if(candidate->inUse && strcmp(candidate->memberId, memberId) == 0)
        {
            bluetoothClient = candidate;
            break;
        }
        if(availableClient == NULL && (!candidate->inUse || !candidate->bluetoothReceiving))
        {
            availableClient = candidate;
        }
    }
    pthread_mutex_unlock(&clientMutex);
    if(bluetoothClient == NULL)
    {
        bluetoothClient = availableClient;
        if(bluetoothClient == NULL)
        {
            RLOG_WARN("Bluetooth session limit reached: id=%s", memberId);
            pthread_mutex_unlock(&bluetoothConnectMutex);
            errno = ENOSPC;
            return -1;
        }
        DisconnectClientBluetooth(bluetoothClient);
        pthread_mutex_lock(&clientMutex);
        memcpy(bluetoothClient->memberId, memberId, memberIdLength + 1);
        bluetoothClient->inUse = 1;
        pthread_mutex_unlock(&clientMutex);
    }

    result = requestedMac == NULL ? ConnectMemberBluetoothDevice(bluetoothClient) : ConnectRegisteredBluetoothDevice(bluetoothClient, requestedMac);
    connectError = errno;
    pthread_mutex_lock(&clientMutex);
    keepSession = bluetoothClient->bluetoothReceiving;
    pthread_mutex_unlock(&clientMutex);
    if(result != 0 && !keepSession)
    {
        DisconnectClientBluetooth(bluetoothClient);
        pthread_mutex_lock(&clientMutex);
        bluetoothClient->inUse = 0;
        pthread_mutex_unlock(&clientMutex);
    }
    pthread_mutex_unlock(&bluetoothConnectMutex);
    errno = connectError;
    return result;
}

static int ConnectMemberBluetoothDevice(ClientInfo *client)
{
    return ConnectRegisteredBluetoothDevice(client, NULL);
}

static int ConnectRegisteredBluetoothDevice(ClientInfo *client, const char *requestedMac)
{
    BluetoothDeviceRecord deviceRecord;
    const char *source = client->ip[0] != '\0' ? client->ip : "BT";
    uint8_t rfcommChannel;
    int queryResult;
    int bluetoothFd;

    queryResult = GetMemberBluetoothDevice(client->memberId, strlen(client->memberId), &deviceRecord);
    if(queryResult == 0)
    {
        RLOG_INFO("[%s] HC-05 registration required: id=%s", source, client->memberId);
        return 1;
    }
    if(queryResult < 0)
    {
        RLOG_WARN("[%s] HC-05 database lookup failed: id=%s", source, client->memberId);
        errno = EIO;
        return -1;
    }

    if(requestedMac != NULL && strcasecmp(requestedMac, deviceRecord.mac) != 0)
    {
        RLOG_WARN("[BT] ID/MAC binding mismatch: id=%s", client->memberId);
        errno = EACCES;
        return -1;
    }

    pthread_mutex_lock(&clientMutex);
    if(client->inUse && client->bluetoothReceiving && strcasecmp(client->bluetoothMac, deviceRecord.mac) == 0)
    {
        pthread_mutex_unlock(&clientMutex);
        RLOG_INFO("Bluetooth already connected: id=%s, mac=%s", client->memberId, deviceRecord.mac);
        return 0;
    }
    for(int index = 0; index < MAX_CLNT; ++index)
    {
        const ClientInfo *other = &bluetoothClients[index];

        if(other != client && other->inUse && other->bluetoothReceiving && strcasecmp(other->bluetoothMac, deviceRecord.mac) == 0)
        {
            pthread_mutex_unlock(&clientMutex);
            RLOG_WARN("Bluetooth MAC already connected to another member: id=%s, mac=%s", client->memberId, deviceRecord.mac);
            errno = EADDRINUSE;
            return -1;
        }
    }
    pthread_mutex_unlock(&clientMutex);

    /* A stopped receiver's RFCOMM socket must be released before reconnecting. */
    DisconnectClientBluetooth(client);
    bluetoothFd = ConnectBluetoothDevice(deviceRecord.mac, BLUETOOTH_CONNECT_TIMEOUT_MS, &rfcommChannel);
    if(bluetoothFd < 0)
    {
        RLOG_WARN("[%s] HC-05 connection failed: id=%s, mac=%s: %s", source, client->memberId, deviceRecord.mac, strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&clientMutex);
    client->bluetoothFd = bluetoothFd;
    memcpy(client->bluetoothMac, deviceRecord.mac, sizeof(client->bluetoothMac));
    pthread_mutex_unlock(&clientMutex);
    if(StartBluetoothReceive(client) != 0)
    {
        int receiveError = errno;

        DisconnectClientBluetooth(client);
        RLOG_ERROR("[%s] Bluetooth receiver initialization failed: id=%s, mac=%s: %s", source, client->memberId, deviceRecord.mac, strerror(receiveError));
        errno = receiveError;
        return -1;
    }
    RLOG_INFO("[%s] HC-05 connected: id=%s, mac=%s, channel=%u", source, client->memberId, client->bluetoothMac, (unsigned int)rfcommChannel);
    return 0;
}

static int StartBluetoothReceive(ClientInfo *client)
{
    BluetoothReceiveContext *context;
    int createResult;

    context = malloc(sizeof(*context));
    if(context == NULL)
    {
        return -1;
    }

    context->stopFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if(context->stopFd < 0)
    {
        free(context);
        return -1;
    }
    pthread_mutex_lock(&clientMutex);
    context->owner = client;
    context->bluetoothFd = client->bluetoothFd;
    memcpy(context->memberId, client->memberId, sizeof(context->memberId));
    memcpy(context->mac, client->bluetoothMac, sizeof(context->mac));
    client->bluetoothStopFd = context->stopFd;
    client->bluetoothReceiving = 1;

    createResult = pthread_create(&client->bluetoothReceiveThread, NULL, ReceiveBluetooth, context);
    if(createResult != 0)
    {
        close(context->stopFd);
        client->bluetoothStopFd = -1;
        client->bluetoothReceiving = 0;
        pthread_mutex_unlock(&clientMutex);
        free(context);
        errno = createResult;
        return -1;
    }
    client->bluetoothReceiveStarted = 1;
    pthread_mutex_unlock(&clientMutex);
    return 0;
}

static void StopBluetoothReceive(int bluetoothFd, int stopFd, pthread_t receiveThread, int receiveStarted)
{
    if(receiveStarted)
    {
        uint64_t stopSignal = 1;
        ssize_t writeResult;

        do
        {
            writeResult = write(stopFd, &stopSignal, sizeof(stopSignal));
        }
        while(writeResult < 0 && errno == EINTR);

        if(writeResult < 0 && errno != EAGAIN)
        {
            RLOG_ERROR("Bluetooth receiver stop signal: %s", strerror(errno));
            shutdown(bluetoothFd, SHUT_RDWR);
        }
        pthread_join(receiveThread, NULL);
    }

    if(stopFd >= 0)
    {
        close(stopFd);
    }
    DisconnectBluetoothDevice(bluetoothFd);
}

static void DisconnectClientBluetooth(ClientInfo *client)
{
    int bluetoothFd;
    int stopFd;
    int receiveStarted;
    pthread_t receiveThread;

    pthread_mutex_lock(&clientMutex);
    bluetoothFd = client->bluetoothFd;
    stopFd = client->bluetoothStopFd;
    receiveThread = client->bluetoothReceiveThread;
    receiveStarted = client->bluetoothReceiveStarted;
    client->bluetoothFd = -1;
    client->bluetoothStopFd = -1;
    client->bluetoothReceiveStarted = 0;
    client->bluetoothReceiving = 0;
    memset(client->bluetoothMac, 0, sizeof(client->bluetoothMac));
    pthread_mutex_unlock(&clientMutex);
    StopBluetoothReceive(bluetoothFd, stopFd, receiveThread, receiveStarted);
}

static void *ReceiveBluetooth(void *arg)
{
    BluetoothReceiveContext *context = (BluetoothReceiveContext *)arg;
    char label[BUF_SIZE];
    PacketConnection connection =
    {
        .transport = PACKET_TRANSPORT_BLUETOOTH,
        .context = context,
        .label = label,
        .receiveAll = ReceiveBluetoothPacketData,
        .sendAll = SendBluetoothPacketData,
        .waitForData = WaitForBluetoothPacketData
    };

    snprintf(label, sizeof(label), "BT id=%s mac=%s", context->memberId, context->mac);
    ReceivePackets(&connection);

    pthread_mutex_lock(&clientMutex);
    if(context->owner->bluetoothFd == context->bluetoothFd && context->owner->bluetoothStopFd == context->stopFd)
    {
        context->owner->bluetoothReceiving = 0;
    }
    pthread_mutex_unlock(&clientMutex);
    RLOG_INFO("[BT id=%s mac=%s] Receiver stopped", context->memberId, context->mac);
    free(context);
    return NULL;
}

static void LogBluetoothData(const BluetoothReceiveContext *context, const uint8_t *data, size_t length)
{
#if RLOG_LEVEL >= RLOG_LEVEL_DEBUG
    char text[RLOG_LINE_SIZE];
    size_t textLength = 0;
    size_t i;

    if(RLogGetLevel() < RLOG_LEVEL_DEBUG)
    {
        return;
    }
    /* Byte logging is separate from the shared OK/RQ/DATA packet decoder; RLog truncates long lines. */
    for(i = 0; i < length && textLength + 5 < sizeof(text); i++)
    {
        if(data[i] >= ' ' && data[i] <= '~' && data[i] != '\\')
        {
            text[textLength++] = (char)data[i];
        }
        else if(data[i] == '\r')
        {
            text[textLength++] = '\\';
            text[textLength++] = 'r';
        }
        else if(data[i] == '\n')
        {
            text[textLength++] = '\\';
            text[textLength++] = 'n';
        }
        else if(data[i] == '\\')
        {
            text[textLength++] = '\\';
            text[textLength++] = '\\';
        }
        else
        {
            textLength += (size_t)snprintf(text + textLength, sizeof(text) - textLength, "\\x%02X", (unsigned int)data[i]);
        }
    }
    text[textLength] = '\0';
    RLOG_DEBUG("[BT id=%s mac=%s] RX %zu bytes: %s", context->memberId, context->mac, length, text);
#else
    (void)context;
    (void)data;
    (void)length;
#endif
}

static int ProcessChatData(PacketConnection *connection, const uint8_t *data, size_t length)
{
    RLOG_INFO("[%s] %.*s", connection->label, (int)length, (const char *)data);
    return 0;
}

static void *SendClient(void *arg)
{
    ClientInfo *client = (ClientInfo *)arg;
    uint8_t sendBuffer[SEND_BUFFER_SIZE];

    while(1)
    {
        size_t sendLength;
        int socketFd;
        int sendResult;

        pthread_mutex_lock(&client->sendMutex);
        while(!client->sendPending && client->connected)
        {
            pthread_cond_wait(&client->sendCond, &client->sendMutex);
        }

        if(!client->connected && !client->sendPending)
        {
            pthread_mutex_unlock(&client->sendMutex);
            break;
        }

        sendLength = client->sendLength;
        socketFd = client->fd;
        memcpy(sendBuffer, client->sendData, sendLength);
        pthread_mutex_unlock(&client->sendMutex);

        sendResult = SendAll(client, sendBuffer, sendLength);

        pthread_mutex_lock(&client->sendMutex);
        client->sendResult = sendResult;
        client->sendPending = 0;
        client->sendComplete = 1;
        if(sendResult != 0)
        {
            client->connected = 0;
        }
        pthread_cond_broadcast(&client->sendCond);
        pthread_mutex_unlock(&client->sendMutex);

        if(sendResult != 0)
        {
            shutdown(socketFd, SHUT_RDWR);
            break;
        }
    }

    UnregisterClientThread(client);
    return NULL;
}

static void *ReceiveClient(void *arg)
{
    ClientInfo *client = (ClientInfo *)arg;
    PacketConnection connection =
    {
        .transport = PACKET_TRANSPORT_TCP,
        .context = client,
        .label = client->ip,
        .receiveAll = ReceiveTcpPacketData,
        .sendAll = SendTcpPacketData,
        .waitForData = WaitForTcpPacketData
    };

    ReceivePackets(&connection);
    StopClient(client);
    UnregisterClientThread(client);
    return NULL;
}

static int ValidatePacketPermission(PacketConnection *connection, uint16_t cmd)
{
    if(connection->transport == PACKET_TRANSPORT_BLUETOOTH)
    {
        /* This link belongs to an already registered member/MAC binding. */
        if(cmd == REQ_LOGIN || cmd == REQ_BT_REGISTER || cmd == REQ_BT_CONNECT)
        {
            RLOG_WARN("Management command not allowed from %s: cmd=0x%04X", connection->label, (unsigned int)cmd);
            return -1;
        }
    }
    else
    {
        ClientInfo *client = (ClientInfo *)connection->context;

        if(cmd != REQ_LOGIN && cmd != REQ_BT_CONNECT && !client->authenticated)
        {
            RLOG_WARN("Unauthenticated command from %s: cmd=0x%04X", connection->label, (unsigned int)cmd);
            return -1;
        }
    }
    return 0;
}

static void ReceivePackets(PacketConnection *connection)
{
    uint8_t headerData[HEADER_SIZE];
    uint8_t receiveData[MAX_PAYLOAD_SIZE] = {0};

    while(1)
    {
        HeaderData header;
        const PacketHandler *packetHandler;
        int processResult;

        if(connection->receiveAll(connection->context, headerData, sizeof(headerData)) != 0)
        {
            break;
        }
        DecodePacketHeader(headerData, &header);

        packetHandler = FindPacketHandler(header.cmd);
        if(packetHandler == NULL)
        {
            RLOG_WARN("Unsupported command from %s: cmd=0x%04X", connection->label, (unsigned int)header.cmd);
            break;
        }
        if(header.length < packetHandler->minDataLength || header.length > packetHandler->maxDataLength)
        {
            RLOG_WARN("Invalid data length from %s: cmd=0x%04X, length=%u", connection->label, (unsigned int)header.cmd, (unsigned int)header.length);
            break;
        }
        if(ValidatePacketPermission(connection, header.cmd) != 0)
        {
            break;
        }

        /* The payload follows the header directly; a sender that stalls is dropped. */
        if(header.length > 0 && connection->waitForData(connection->context, DATA_WAIT_TIMEOUT_MS) <= 0)
        {
            RLOG_WARN("Payload timeout from %s: cmd=0x%04X", connection->label, (unsigned int)header.cmd);
            break;
        }
        if(connection->receiveAll(connection->context, receiveData, header.length) != 0)
        {
            break;
        }
        if(CheckPacketCrc(headerData, &header, receiveData) != 0)
        {
            RLOG_WARN("CRC mismatch from %s: cmd=0x%04X", connection->label, (unsigned int)header.cmd);
            break;
        }

        processResult = packetHandler->processData(connection, receiveData, header.length);
        if(header.cmd == REQ_LOGIN || header.cmd == REQ_BT_REGISTER || header.cmd == REQ_BT_CONNECT)
        {
            sodium_memzero(receiveData, header.length);
        }
        if(processResult != 0)
        {
            break;
        }
    }

    sodium_memzero(receiveData, sizeof(receiveData));
}

static void LogTlsErrors(uint8_t level)
{
    unsigned long tlsError;

    while((tlsError = ERR_get_error()) != 0)
    {
        char text[256];

        ERR_error_string_n(tlsError, text, sizeof(text));
        if(level == RLOG_LEVEL_ERROR)
        {
            RLOG_ERROR("TLS: %s", text);
        }
        else
        {
            RLOG_WARN("TLS: %s", text);
        }
    }
}
