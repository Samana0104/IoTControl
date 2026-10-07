#include "RTCPServer.h"
#include "RDatabase.h"
#include "RCommand.h"
#include "RDatabaseCommand.h"
#include "RPacket.h"
#include "RNetwork.h"
#include "RLog.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define LISTEN_BACKLOG 5
#define SERVER_POLL_TIMEOUT_MS 500
/* Packet handlers may block on DB, password hashing, BT pairing (30 s) or BT connect (5 s). */
#define SERVER_WORKER_COUNT 8
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

static volatile sig_atomic_t serverStopRequested;

static void HandleServerStopSignal(int signalNumber);
static void ShowServerConsole(const ServerConsole *console);
static void ReadServerConsole(ServerConsole *console, TCPServer *server);
static void ProcessServerConsoleInput(ServerConsole *console, const char *input, size_t length, TCPServer *server);
static SSL_CTX *CreateTlsServerContext(void);
static char *TrimTlsConfigText(char *text);
static int ReadTlsConfigLine(FILE *file, char *line, size_t lineSize);
static int LoadTlsConfig(const char *filePath, TlsConfig *config);

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

    if(RNetStart(RPacketProcess, SERVER_WORKER_COUNT) != 0)
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

        tls = SSL_new(server->tlsContext);
        if(tls == NULL || SSL_set_fd(tls, clientSocket) != 1)
        {
            RLOG_ERROR("SSL_new/SSL_set_fd failed");
            ERR_clear_error();
            SSL_free(tls);
            close(clientSocket);
            continue;
        }
        /* RNetwork owns the socket and TLS from here, even on failure; a worker runs the handshake. */
        RNetOpenTcp(clientSocket, tls, &clientAddress);
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
    RLOG_INFO("%s", server->socket >= 0 ? "Server CLI input closed; server continues running." : "Server CLI input closed before server start.");
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
