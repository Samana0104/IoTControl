#include "IotClient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int ParsePort(const char *port);
static int SendAll(int socketFd, const void *buffer, size_t length);
static int ReceiveAll(int socketFd, void *buffer, size_t length);

void InitializeClient(IotClient *client)
{
    if(client != NULL)
    {
        client->fd = -1;
    }
}

int ConnectClient(IotClient *client, const char *serverIp, const char *port)
{
    int serverPort;
    int socketFd;
    struct sockaddr_in serverAddress;

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

    if(connect(socketFd,
               (struct sockaddr *)&serverAddress,
               sizeof(serverAddress)) < 0)
    {
        close(socketFd);
        return -1;
    }

    client->fd = socketFd;
    return 0;
}

int SendChatMessage(IotClient *client, const char *message)
{
    uint8_t sendHeader[HEADER_SIZE];
    uint8_t responseHeader[HEADER_SIZE];
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

    sendHeader[0] = HEADER_OK_0;
    sendHeader[1] = HEADER_OK_1;
    sendHeader[2] = CMD_CHAT_DATA;
    sendHeader[3] = (uint8_t)messageLength;

    if(SendAll(client->fd, sendHeader, sizeof(sendHeader)) != 0)
    {
        return -1;
    }

    if(ReceiveAll(client->fd, responseHeader, sizeof(responseHeader)) != 0)
    {
        return -1;
    }

    if(responseHeader[0] != HEADER_REQUEST_0 ||
       responseHeader[1] != HEADER_REQUEST_1 ||
       responseHeader[2] != CMD_CHAT_DATA ||
       responseHeader[3] != 0)
    {
        errno = EPROTO;
        return -1;
    }

    return SendAll(client->fd, message, messageLength);
}

void DisconnectClient(IotClient *client)
{
    if(client == NULL || client->fd < 0)
    {
        return;
    }

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

static int SendAll(int socketFd, const void *buffer, size_t length)
{
    const uint8_t *current = (const uint8_t *)buffer;
    size_t sentLength = 0;

    while(sentLength < length)
    {
        ssize_t result = send(socketFd,
                              current + sentLength,
                              length - sentLength,
                              MSG_NOSIGNAL);
        if(result == 0)
        {
            return -1;
        }
        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        sentLength += (size_t)result;
    }

    return 0;
}

static int ReceiveAll(int socketFd, void *buffer, size_t length)
{
    uint8_t *current = (uint8_t *)buffer;
    size_t receivedLength = 0;

    while(receivedLength < length)
    {
        ssize_t result = recv(socketFd,
                              current + receivedLength,
                              length - receivedLength,
                              0);
        if(result == 0)
        {
            return -1;
        }
        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        receivedLength += (size_t)result;
    }

    return 0;
}
