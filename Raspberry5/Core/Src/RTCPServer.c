#include "RTCPServer.h"
#include "RBluetooth.h"
#include "RDatabase.h"
#include "RCommand.h"
#include "RCtrlCon.h"
#include "RCtrlDht.h"
#include "RCtrlFan.h"
#include "RDatabaseCommand.h"
#include "RSession.h"
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
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define LISTEN_BACKLOG 5
#define DATA_WAIT_TIMEOUT_MS 5000
#define TLS_HANDSHAKE_TIMEOUT_SECONDS 5
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

/* Console input is line-buffered here and each line is handed to RCommandExecute(). */
typedef struct _ServerConsole
{
    int enabled;
    int interactive;
    int discardingInput;
    size_t inputLength;
    char input[SERVER_CONSOLE_INPUT_SIZE];
} ServerConsole;

typedef int (*ProcessPacketData)(RSession *session, const uint8_t *data, size_t length);

/* Exactly one of processData (server session/state) or controllerData (device data) is set. */
typedef struct _PacketHandler
{
    uint16_t cmd;
    size_t minDataLength;
    size_t maxDataLength;
    ProcessPacketData processData;
    RCtrlHandler controllerData;
} PacketHandler;

static pthread_mutex_t bluetoothPairMutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t serverStopRequested;

static void HandleServerStopSignal(int signalNumber);
static void ShowServerConsole(const ServerConsole *console);
static void ReadServerConsole(ServerConsole *console, TCPServer *server);
static void ProcessServerConsoleInput(ServerConsole *console, const char *input, size_t length, TCPServer *server);
static int SetSocketTimeout(int socketFd, int timeoutSeconds);
static SSL_CTX *CreateTlsServerContext(void);
static char *TrimTlsConfigText(char *text);
static int ReadTlsConfigLine(FILE *file, char *line, size_t lineSize);
static int LoadTlsConfig(const char *filePath, TlsConfig *config);
static int ValidatePacketPermission(RSession *session, uint16_t cmd);
static void ReceivePackets(RSession *session);
static const PacketHandler *FindPacketHandler(uint16_t cmd);
static int SendAck(RSession *session, uint16_t reqCmd, int succeeded);
static int ProcessControllerData(RSession *session, RCtrlHandler handler, const uint8_t *data, size_t length);
static int ProcessMemData(RSession *session, const uint8_t *data, size_t length);
static int ProcessChatData(RSession *session, const uint8_t *data, size_t length);
static int ProcessBluetoothRegisterData(RSession *session, const uint8_t *data, size_t length);
static int ProcessBluetoothConnectData(RSession *session, const uint8_t *data, size_t length);
static int RequestRegisteredBluetoothConnection(const char *memberId, const char *requestedMac);

static const PacketHandler PACKET_HANDLERS[] =
{
    /* Server: login and Bluetooth session management */
    {REQ_LOGIN, MEM_DATA_SIZE, MEM_DATA_SIZE, ProcessMemData, NULL},
    {REQ_BT_REGISTER, BLUETOOTH_REGISTER_DATA_SIZE, BLUETOOTH_REGISTER_DATA_SIZE, ProcessBluetoothRegisterData, NULL},
    {REQ_BT_CONNECT, BLUETOOTH_CONNECT_DATA_SIZE, BLUETOOTH_CONNECT_DATA_SIZE, ProcessBluetoothConnectData, NULL},
    {NFY_CHAT, 0, MAX_CHAT_SIZE, ProcessChatData, NULL},
    /* Controller: device data and control results */
    {NFY_DHT, DHT_DATA_SIZE, DHT_DATA_SIZE, NULL, RCtrlDhtReceive},
    {NFY_FAN, FAN_DATA_SIZE, FAN_DATA_SIZE, NULL, RCtrlFanReceive},
    {NFY_CON, CON_DATA_SIZE, CON_DATA_SIZE, NULL, RCtrlConReceive},
    {ACK_FAN, RESULT_DATA_SIZE, RESULT_DATA_SIZE, NULL, RCtrlFanReceiveAck}
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

    if(RSessionInit(ReceivePackets) != 0)
    {
        RLOG_ERROR("Session initialization failed");
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
        SSL *tls;

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
            ERR_clear_error();
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

        /* The session owns the socket and TLS from here, even on failure. */
        RSessionOpenTcp(clientSocket, tls, &clientAddress);
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
    RSessionCloseAll();
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
        ERR_clear_error();
        return NULL;
    }

    if(SSL_CTX_set_min_proto_version(tlsContext, TLS1_2_VERSION) != 1 || SSL_CTX_use_certificate_chain_file(tlsContext, config.certificateFile) != 1 || SSL_CTX_use_PrivateKey_file(tlsContext, config.privateKeyFile, SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(tlsContext) != 1)
    {
        RLOG_ERROR("TLS certificate initialization failed");
        ERR_clear_error();
        SSL_CTX_free(tlsContext);
        return NULL;
    }

    SSL_CTX_set_options(tlsContext, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_mode(tlsContext, SSL_MODE_AUTO_RETRY);
    return tlsContext;
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

/* Device data goes to the Controller with the member that owns this link. */
static int ProcessControllerData(RSession *session, RCtrlHandler handler, const uint8_t *data, size_t length)
{
    char memberId[MEM_ID_SIZE + 1];
    RCtrlContext context;

    if(RSessionGetMemberId(session, memberId) != 0)
    {
        return -1;
    }
    context.label = RSessionGetLabel(session);
    context.memberId = memberId;
    context.fd = RSessionGetFd(session);
    return handler(&context, data, length);
}

static int SendAck(RSession *session, uint16_t reqCmd, int succeeded)
{
    uint8_t frame[HEADER_SIZE + RESULT_DATA_SIZE];

    MakeAckPacket(frame, sizeof(frame), reqCmd, succeeded ? RESULT_SUCCESS : RESULT_FAIL);
    return RSessionSendFrame(session, frame, sizeof(frame));
}

static int ProcessMemData(RSession *session, const uint8_t *data, size_t length)
{
    MemData memData;
    char memberId[MEM_ID_SIZE + 1];
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
    memcpy(memberId, memData.id, memberIdLength);
    memberId[memberIdLength] = '\0';
    sodium_memzero(&memData, sizeof(memData));

    if(verifyResult != 1)
    {
        RSessionLogout(session);
        RLOG_WARN("[%s] Member authentication failed", RSessionGetLabel(session));
        SendAck(session, REQ_LOGIN, 0);
        return -1;
    }

    RSessionLogin(session, memberId, memberIdLength);
    RLOG_INFO("[%s] Member authenticated: id=%s", RSessionGetLabel(session), memberId);
    return SendAck(session, REQ_LOGIN, 1);
}

static int ProcessBluetoothRegisterData(RSession *session, const uint8_t *data, size_t length)
{
    BluetoothRegisterData registerData;
    BluetoothDeviceRecord existingDevice;
    char memberId[MEM_ID_SIZE + 1];
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    char pin[BLUETOOTH_PIN_SIZE + 1];
    size_t macLength;
    size_t pinLength;
    int queryResult;
    int registerResult;
    int processResult = -1;

    if(RSessionGetMemberId(session, memberId) != 0 || ReadBluetoothRegisterData(data, length, &registerData) != 0)
    {
        return -1;
    }
    pthread_mutex_lock(&bluetoothPairMutex);
    macLength = strnlen(registerData.mac, BLUETOOTH_MAC_SIZE);
    pinLength = strnlen(registerData.pin, BLUETOOTH_PIN_SIZE);
    if(macLength != BLUETOOTH_MAC_SIZE || pinLength == 0)
    {
        RLOG_WARN("[%s] Invalid HC-05 registration data: id=%s", RSessionGetLabel(session), memberId);
        goto cleanup;
    }

    memcpy(bluetoothMac, registerData.mac, macLength);
    bluetoothMac[macLength] = '\0';
    memcpy(pin, registerData.pin, pinLength);
    pin[pinLength] = '\0';

    queryResult = GetMemberBluetoothDevice(memberId, strlen(memberId), &existingDevice);
    if(queryResult != 0)
    {
        RLOG_WARN("[%s] HC-05 registration rejected: id=%s, reason=%s", RSessionGetLabel(session), memberId, queryResult > 0 ? "already registered" : "database error");
        goto cleanup;
    }

    if(PairBluetoothDevice(bluetoothMac, pin, BLUETOOTH_PAIR_TIMEOUT_SECONDS) != 0)
    {
        RLOG_WARN("[%s] HC-05 pairing failed: id=%s, mac=%s: %s", RSessionGetLabel(session), memberId, bluetoothMac, strerror(errno));
        goto cleanup;
    }

    registerResult = RegisterMemberBluetoothDevice(memberId, strlen(memberId), bluetoothMac, macLength);
    if(registerResult != 1)
    {
        RLOG_WARN("[%s] HC-05 database registration failed: id=%s, reason=%s", RSessionGetLabel(session), memberId, registerResult == 0 ? "already registered" : "database error");
        goto cleanup;
    }

    if(RequestMemberBluetoothConnection(memberId) != 0)
    {
        goto cleanup;
    }

    RLOG_INFO("[%s] HC-05 registered: id=%s, mac=%s", RSessionGetLabel(session), memberId, bluetoothMac);
    processResult = 0;

cleanup:
    pthread_mutex_unlock(&bluetoothPairMutex);
    sodium_memzero(pin, sizeof(pin));
    sodium_memzero(&registerData, sizeof(registerData));
    if(SendAck(session, REQ_BT_REGISTER, processResult == 0) != 0)
    {
        processResult = -1;
    }
    return processResult;
}

static int ProcessBluetoothConnectData(RSession *session, const uint8_t *data, size_t length)
{
    BluetoothConnectData request;
    int connected = 0;
    char memberId[MEM_ID_SIZE + 1];
    char requestedMac[BLUETOOTH_MAC_TEXT_SIZE];
    size_t memberIdLength;
    size_t passwordLength;
    int verifyResult;

    if(RSessionGetType(session) != SESSION_TCP || data == NULL || ReadBluetoothConnectData(data, length, &request) != 0)
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
    RLOG_INFO("[%s] Bluetooth request: id=%s, result=%s", RSessionGetLabel(session), verifyResult == 1 ? memberId : "-", connected ? "connected" : "failed");
    /* BT is server-owned already; losing this result recipient must not close it. */
    return SendAck(session, REQ_BT_CONNECT, connected);
}

int RequestMemberBluetoothConnection(const char *memberId)
{
    return RequestRegisteredBluetoothConnection(memberId, NULL);
}

/* Looks up the member's registered HC-05 and hands it to the BT session.
   requestedMac != NULL: the caller's MAC must match the DB binding. */
static int RequestRegisteredBluetoothConnection(const char *memberId, const char *requestedMac)
{
    BluetoothDeviceRecord deviceRecord;
    size_t memberIdLength;
    int queryResult;

    if(memberId == NULL || (memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1)) == 0 || memberIdLength > MEM_ID_SIZE)
    {
        errno = EINVAL;
        return -1;
    }
    if(serverStopRequested)
    {
        errno = ECANCELED;
        return -1;
    }

    queryResult = GetMemberBluetoothDevice(memberId, memberIdLength, &deviceRecord);
    if(queryResult == 0)
    {
        RLOG_INFO("[BT] HC-05 registration required: id=%s", memberId);
        return 1;
    }
    if(queryResult < 0)
    {
        RLOG_WARN("[BT] HC-05 database lookup failed: id=%s", memberId);
        errno = EIO;
        return -1;
    }
    if(requestedMac != NULL && strcasecmp(requestedMac, deviceRecord.mac) != 0)
    {
        RLOG_WARN("[BT] ID/MAC binding mismatch: id=%s", memberId);
        errno = EACCES;
        return -1;
    }
    return RSessionOpenBt(memberId, deviceRecord.mac) >= 0 ? 0 : -1;
}

static int ProcessChatData(RSession *session, const uint8_t *data, size_t length)
{
    RLOG_INFO("[%s] %.*s", RSessionGetLabel(session), (int)length, (const char *)data);
    return 0;
}

static int ValidatePacketPermission(RSession *session, uint16_t cmd)
{
    if(RSessionGetType(session) == SESSION_BLUETOOTH)
    {
        /* This link belongs to an already registered member/MAC binding. */
        if(cmd == REQ_LOGIN || cmd == REQ_BT_REGISTER || cmd == REQ_BT_CONNECT)
        {
            RLOG_WARN("Management command not allowed from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
            return -1;
        }
    }
    else
    {
        if(cmd != REQ_LOGIN && cmd != REQ_BT_CONNECT && !RSessionIsAuthenticated(session))
        {
            RLOG_WARN("Unauthenticated command from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
            return -1;
        }
        /* The server sends no REQ to TCP clients, so no ACK is expected from them. */
        if(IS_ACK(cmd))
        {
            RLOG_WARN("Unexpected ACK from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
            return -1;
        }
    }
    return 0;
}

/* RSessionReceiver: runs on each TCP/BT session's receive thread until the link fails. */
static void ReceivePackets(RSession *session)
{
    uint8_t headerData[HEADER_SIZE];
    uint8_t receiveData[MAX_PAYLOAD_SIZE] = {0};

    while(1)
    {
        HeaderData header;
        const PacketHandler *packetHandler;
        int processResult;

        if(RSessionReceive(session, headerData, sizeof(headerData)) != 0)
        {
            break;
        }
        DecodePacketHeader(headerData, &header);

        packetHandler = FindPacketHandler(header.cmd);
        if(packetHandler == NULL)
        {
            RLOG_WARN("Unsupported command from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)header.cmd);
            break;
        }
        if(header.length < packetHandler->minDataLength || header.length > packetHandler->maxDataLength)
        {
            RLOG_WARN("Invalid data length from %s: cmd=0x%04X, length=%u", RSessionGetLabel(session), (unsigned int)header.cmd, (unsigned int)header.length);
            break;
        }
        if(ValidatePacketPermission(session, header.cmd) != 0)
        {
            break;
        }

        /* The payload follows the header directly; a sender that stalls is dropped. */
        if(header.length > 0 && RSessionWaitForData(session, DATA_WAIT_TIMEOUT_MS) <= 0)
        {
            RLOG_WARN("Payload timeout from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)header.cmd);
            break;
        }
        if(RSessionReceive(session, receiveData, header.length) != 0)
        {
            break;
        }
        if(CheckPacketCrc(headerData, &header, receiveData) != 0)
        {
            RLOG_WARN("CRC mismatch from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)header.cmd);
            break;
        }

        processResult = packetHandler->processData != NULL ? packetHandler->processData(session, receiveData, header.length) : ProcessControllerData(session, packetHandler->controllerData, receiveData, header.length);
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
