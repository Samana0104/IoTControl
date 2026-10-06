#include "IotClient.h"
#include "IoTPacket.h"

#include <arpa/inet.h>
#include <errno.h>
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
#include <sys/time.h>
#include <unistd.h>

#define TLS_HANDSHAKE_TIMEOUT_SECONDS 5

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
    const char *certificateAuthorityFile;
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

    certificateAuthorityFile = getenv("IOT_TLS_CA_FILE");
    if(certificateAuthorityFile == NULL)
    {
        fputs("IOT_TLS_CA_FILE must be set\n", stderr);
        errno = EINVAL;
        return -1;
    }

    serverPort = ParsePort(port);
    if(serverPort < 0)
    {
        errno = EINVAL;
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
    if(tlsContext == NULL || SSL_CTX_set_min_proto_version(tlsContext, TLS1_2_VERSION) != 1 || SSL_CTX_load_verify_locations(tlsContext, certificateAuthorityFile, NULL) != 1)
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

    if(SendAll(client, sendHeader, sizeof(sendHeader)) != 0 || ReceiveAll(client, responseHeader, sizeof(responseHeader)) != 0)
    {
        return -1;
    }

    if(responseHeader[0] != HEADER_REQUEST_0 || responseHeader[1] != HEADER_REQUEST_1 || responseHeader[2] != cmd || responseHeader[3] != 0)
    {
        errno = EPROTO;
        return -1;
    }

    return SendAll(client, data, length);
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
