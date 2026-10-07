#include "RClient.h"
#include "IoTPacket.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <sodium.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define TLS_HANDSHAKE_TIMEOUT_SECONDS 5
#define BLUETOOTH_RESULT_TIMEOUT_SECONDS 90
#define TLS_CLIENT_CONFIG_PATH_SIZE 4096
#define TLS_CLIENT_CONFIG_LINE_SIZE (TLS_CLIENT_CONFIG_PATH_SIZE + 128)

typedef struct _TlsClientConfig
{
    char certificateAuthorityFile[TLS_CLIENT_CONFIG_PATH_SIZE];
} TlsClientConfig;

static char *TrimTlsClientConfigText(char *text);
static int ReadTlsClientConfigLine(FILE *file, char *line, size_t lineSize);
static int LoadTlsClientConfig(const char *filePath, TlsClientConfig *config);
static int ParsePort(const char *port);
static int SetSocketTimeout(int socketFd, int timeoutSeconds);
static int SendPacket(IotClient *client, uint8_t cmd, const void *data, size_t length);
static int SendAll(IotClient *client, const void *buffer, size_t length);
static int ReceiveAll(IotClient *client, void *buffer, size_t length);

void InitializeClient(IotClient *client)
{
    if(client != NULL)
    {
        client->fd = -1;
        client->tlsContext = NULL;
        client->tls = NULL;
    }
}

int ConnectClient(IotClient *client, const char *serverIp, const char *port)
{
    int serverPort;
    int socketFd;
    struct sockaddr_in serverAddress;
    TlsClientConfig config;
    SSL_CTX *tlsContext = NULL;
    SSL *tls = NULL;

    if(client == NULL || serverIp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if(client->fd >= 0)
    {
        errno = EISCONN;
        return -1;
    }

    serverPort = ParsePort(port);
    if(serverPort < 0)
    {
        errno = EINVAL;
        return -1;
    }

    if(LoadTlsClientConfig(TLS_CLIENT_CONFIG_FILE, &config) != 0)
    {
        return -1;
    }

    socketFd = socket(PF_INET, SOCK_STREAM, 0);
    if(socketFd < 0)
    {
        return -1;
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons((uint16_t)serverPort);

    if(inet_pton(AF_INET, serverIp, &serverAddress.sin_addr) != 1)
    {
        errno = EINVAL;
        close(socketFd);
        return -1;
    }

    if(connect(socketFd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        close(socketFd);
        return -1;
    }

    if(SetSocketTimeout(socketFd, TLS_HANDSHAKE_TIMEOUT_SECONDS) != 0)
    {
        close(socketFd);
        return -1;
    }

    tlsContext = SSL_CTX_new(TLS_client_method());
    if(tlsContext == NULL || SSL_CTX_set_min_proto_version(tlsContext, TLS1_2_VERSION) != 1 || SSL_CTX_load_verify_locations(tlsContext, config.certificateAuthorityFile, NULL) != 1)
    {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(tlsContext);
        close(socketFd);
        errno = EPROTO;
        return -1;
    }

    SSL_CTX_set_verify(tlsContext, SSL_VERIFY_PEER, NULL);
    tls = SSL_new(tlsContext);
    if(tls == NULL || SSL_set_fd(tls, socketFd) != 1 || X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(tls), serverIp) != 1 || SSL_connect(tls) != 1)
    {
        long verifyResult = tls == NULL ? X509_V_ERR_UNSPECIFIED : SSL_get_verify_result(tls);

        if(verifyResult != X509_V_OK)
        {
            fprintf(stderr, "TLS certificate verification failed: %s\n", X509_verify_cert_error_string(verifyResult));
        }
        else
        {
            fputs("TLS handshake failed\n", stderr);
        }
        ERR_print_errors_fp(stderr);
        SSL_free(tls);
        SSL_CTX_free(tlsContext);
        close(socketFd);
        errno = EPROTO;
        return -1;
    }

    if(SetSocketTimeout(socketFd, 0) != 0)
    {
        SSL_shutdown(tls);
        SSL_free(tls);
        SSL_CTX_free(tlsContext);
        close(socketFd);
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);
    client->fd = socketFd;
    client->tlsContext = tlsContext;
    client->tls = tls;
    return 0;
}

static char *TrimTlsClientConfigText(char *text)
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

static int ReadTlsClientConfigLine(FILE *file, char *line, size_t lineSize)
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

static int LoadTlsClientConfig(const char *filePath, TlsClientConfig *config)
{
    char line[TLS_CLIENT_CONFIG_LINE_SIZE];
    struct stat fileStatus;
    int fileDescriptor = -1;
    FILE *file = NULL;
    int readResult;
    int result = -1;
    int configError = EINVAL;
    int seen = 0;
    size_t lineNumber = 0;

    memset(config, 0, sizeof(*config));
    fileDescriptor = open(filePath, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(fileDescriptor < 0)
    {
        configError = errno;
        fprintf(stderr, "Cannot open client TLS config file '%s': %s\n", filePath, strerror(configError));
        goto cleanup;
    }
    if(fstat(fileDescriptor, &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode))
    {
        fputs("Client TLS config must be a regular file\n", stderr);
        goto cleanup;
    }
    if(fileStatus.st_mode & (S_IRWXG | S_IRWXO))
    {
        configError = EACCES;
        fprintf(stderr, "Client TLS config permissions are too open. Run: chmod 600 %s\n", filePath);
        goto cleanup;
    }
    file = fdopen(fileDescriptor, "r");
    if(file == NULL)
    {
        configError = errno;
        fputs("Cannot read client TLS config file\n", stderr);
        goto cleanup;
    }
    fileDescriptor = -1;
    while((readResult = ReadTlsClientConfigLine(file, line, sizeof(line))) > 0)
    {
        char *key = line;
        char *separator;
        char *value;
        size_t valueLength;

        ++lineNumber;
        if(lineNumber == 1 && strncmp(key, "\xEF\xBB\xBF", 3) == 0)
        {
            key += 3;
        }
        key = TrimTlsClientConfigText(key);
        if(*key == '\0' || *key == '#')
        {
            continue;
        }
        separator = strchr(key, '=');
        if(separator == NULL)
        {
            fprintf(stderr, "Expected KEY=value in client TLS config at line %zu\n", lineNumber);
            goto cleanup;
        }
        *separator = '\0';
        key = TrimTlsClientConfigText(key);
        value = TrimTlsClientConfigText(separator + 1);
        valueLength = strlen(value);
        if(*value == '\'' || *value == '"')
        {
            if(valueLength < 2 || value[valueLength - 1] != *value)
            {
                fprintf(stderr, "Unmatched quotes in client TLS config at line %zu\n", lineNumber);
                goto cleanup;
            }
            value[valueLength - 1] = '\0';
            ++value;
            valueLength -= 2;
        }
        if(strcmp(key, "IOT_TLS_CA_FILE") != 0 || seen)
        {
            fprintf(stderr, "Unknown or duplicate client TLS config key at line %zu\n", lineNumber);
            goto cleanup;
        }
        if(valueLength == 0 || valueLength >= sizeof(config->certificateAuthorityFile))
        {
            fprintf(stderr, "Invalid IOT_TLS_CA_FILE length at line %zu\n", lineNumber);
            goto cleanup;
        }
        memcpy(config->certificateAuthorityFile, value, valueLength + 1);
        seen = 1;
    }
    if(readResult < 0)
    {
        fputs("Unreadable, binary or oversized client TLS config line\n", stderr);
        goto cleanup;
    }
    if(!seen)
    {
        fputs("Missing IOT_TLS_CA_FILE in client TLS config\n", stderr);
        goto cleanup;
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
        errno = configError;
    }
    return result;
}

int AuthenticateClient(IotClient *client, const char *memberId, const char *password)
{
    MemData memData;
    size_t memberIdLength;
    size_t passwordLength;
    int result;

    if(client == NULL || client->fd < 0 || memberId == NULL || password == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    memberIdLength = strlen(memberId);
    passwordLength = strlen(password);
    if(memberIdLength == 0 || memberIdLength > MEM_ID_SIZE || passwordLength == 0 || passwordLength > MEM_PW_SIZE)
    {
        errno = EMSGSIZE;
        return -1;
    }

    memset(&memData, 0, sizeof(memData));
    memcpy(memData.id, memberId, memberIdLength);
    memcpy(memData.pw, password, passwordLength);
    result = SendPacket(client, CMD_MEM_DATA, &memData, sizeof(memData));
    sodium_memzero(&memData, sizeof(memData));
    return result;
}

int RegisterBluetoothDevice(IotClient *client, const char *bluetoothMac, const char *pin)
{
    BluetoothRegisterData registerData;
    size_t macLength;
    size_t pinLength;
    int result;

    if(client == NULL || client->fd < 0 || bluetoothMac == NULL || pin == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    macLength = strlen(bluetoothMac);
    pinLength = strlen(pin);
    if(macLength != BLUETOOTH_MAC_SIZE || pinLength == 0 || pinLength > BLUETOOTH_PIN_SIZE)
    {
        errno = EINVAL;
        return -1;
    }

    memset(&registerData, 0, sizeof(registerData));
    memcpy(registerData.mac, bluetoothMac, macLength);
    memcpy(registerData.pin, pin, pinLength);
    result = SendPacket(client, CMD_BLUETOOTH_REGISTER, &registerData, sizeof(registerData));
    sodium_memzero(&registerData, sizeof(registerData));
    return result;
}

int RequestBluetoothConnection(IotClient *client, const char *memberId, const char *password, const char *bluetoothMac)
{
    BluetoothConnectData request = {0};
    BluetoothConnectResult response;
    uint8_t responseHeader[HEADER_SIZE];
    struct timeval originalTimeout;
    struct timeval resultTimeout = {.tv_sec = BLUETOOTH_RESULT_TIMEOUT_SECONDS};
    socklen_t timeoutLength = sizeof(originalTimeout);
    size_t memberIdLength;
    size_t passwordLength;
    int result = -1;
    int requestError;

    if(client == NULL || client->fd < 0 || client->tls == NULL || memberId == NULL || password == NULL || bluetoothMac == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    memberIdLength = strnlen(memberId, MEM_ID_SIZE + 1);
    passwordLength = strnlen(password, MEM_PW_SIZE + 1);
    if(memberIdLength == 0 || memberIdLength > MEM_ID_SIZE || passwordLength == 0 || passwordLength > MEM_PW_SIZE || strnlen(bluetoothMac, BLUETOOTH_MAC_SIZE + 1) != BLUETOOTH_MAC_SIZE)
    {
        errno = EINVAL;
        return -1;
    }
    if(getsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &originalTimeout, &timeoutLength) != 0 || setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &resultTimeout, sizeof(resultTimeout)) != 0)
    {
        return -1;
    }
    memcpy(request.id, memberId, memberIdLength);
    memcpy(request.pw, password, passwordLength);
    memcpy(request.mac, bluetoothMac, sizeof(request.mac));
    result = SendPacket(client, CMD_BLUETOOTH_CONNECT, &request, sizeof(request));
    sodium_memzero(&request, sizeof(request));
    if(result != 0)
    {
        goto cleanup;
    }
    result = -1;
    do
    {
        if(ReceiveAll(client, responseHeader, sizeof(responseHeader)) != 0)
        {
            goto cleanup;
        }
    }
    while(responseHeader[0] == HEADER_REQUEST_0 && responseHeader[1] == HEADER_REQUEST_1 && responseHeader[3] == RQ_FLAG_RETRY);

    if(responseHeader[0] != HEADER_RESULT_0 || responseHeader[1] != HEADER_RESULT_1 || responseHeader[2] != CMD_BLUETOOTH_CONNECT || responseHeader[3] != sizeof(response))
    {
        errno = EPROTO;
        goto cleanup;
    }
    if(ReceiveAll(client, &response, sizeof(response)) != 0)
    {
        goto cleanup;
    }
    if(response.connected != BLUETOOTH_CONNECT_FAILED && response.connected != BLUETOOTH_CONNECT_SUCCEEDED)
    {
        errno = EPROTO;
        goto cleanup;
    }
    result = response.connected;

cleanup:
    requestError = errno;
    if(setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &originalTimeout, sizeof(originalTimeout)) != 0 && result >= 0)
    {
        return -1;
    }
    errno = requestError;
    return result;
}

int SendDhtData(IotClient *client, const DhtData *data)
{
    if(client == NULL || client->fd < 0 || client->tls == NULL || data == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    return SendPacket(client, CMD_DHT11_DATA, data, sizeof(*data));
}

int SendChatMessage(IotClient *client, const char *message)
{
    size_t messageLength;

    if(client == NULL || client->fd < 0 || message == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    messageLength = strlen(message);
    if(messageLength > MAX_MESSAGE_SIZE)
    {
        errno = EMSGSIZE;
        return -1;
    }

    return SendPacket(client, CMD_CHAT_DATA, message, messageLength);
}

void DisconnectClient(IotClient *client)
{
    if(client == NULL || client->fd < 0)
    {
        return;
    }

    SSL_shutdown(client->tls);
    SSL_free(client->tls);
    SSL_CTX_free(client->tlsContext);
    client->tls = NULL;
    client->tlsContext = NULL;
    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    client->fd = -1;
}

static int ParsePort(const char *port)
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

static int SendPacket(IotClient *client, uint8_t cmd, const void *data, size_t length)
{
    uint8_t sendHeader[HEADER_SIZE];
    uint8_t responseHeader[HEADER_SIZE];

    if(length > MAX_MESSAGE_SIZE)
    {
        errno = EMSGSIZE;
        return -1;
    }

    sendHeader[0] = HEADER_OK_0;
    sendHeader[1] = HEADER_OK_1;
    sendHeader[2] = cmd;
    sendHeader[3] = (uint8_t)length;

    if(SendAll(client, sendHeader, sizeof(sendHeader)) != 0)
    {
        return -1;
    }

    while(1)
    {
        if(ReceiveAll(client, responseHeader, sizeof(responseHeader)) != 0)
        {
            return -1;
        }

        if(responseHeader[0] != HEADER_REQUEST_0 || responseHeader[1] != HEADER_REQUEST_1 || (responseHeader[3] != RQ_FLAG_INITIAL && responseHeader[3] != RQ_FLAG_RETRY))
        {
            errno = EPROTO;
            return -1;
        }

        /* Discard queued retries before checking the current command. */
        if(responseHeader[3] == RQ_FLAG_RETRY)
        {
            continue;
        }

        if(responseHeader[2] != cmd)
        {
            errno = EPROTO;
            return -1;
        }

        return SendAll(client, data, length);
    }
}

static int SendAll(IotClient *client, const void *buffer, size_t length)
{
    const uint8_t *current = (const uint8_t *)buffer;
    size_t sentLength = 0;

    while(sentLength < length)
    {
        int result;
        int tlsError;

        ERR_clear_error();
        result = SSL_write(client->tls, current + sentLength, (int)(length - sentLength));
        tlsError = result > 0 ? SSL_ERROR_NONE : SSL_get_error(client->tls, result);
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

static int ReceiveAll(IotClient *client, void *buffer, size_t length)
{
    uint8_t *current = (uint8_t *)buffer;
    size_t receivedLength = 0;

    while(receivedLength < length)
    {
        int result;
        int tlsError;

        ERR_clear_error();
        result = SSL_read(client->tls, current + receivedLength, (int)(length - receivedLength));
        tlsError = result > 0 ? SSL_ERROR_NONE : SSL_get_error(client->tls, result);
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
