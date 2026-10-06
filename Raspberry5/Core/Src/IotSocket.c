#include "IotSocket.h"
#include "IoTPacket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_BACKLOG 5
#define SEND_BUFFER_SIZE (MAX_MESSAGE_SIZE + HEADER_SIZE)
#define DATA_WAIT_TIMEOUT_MS 5000

typedef struct _ClientInfo
{
    int index;
    int fd;
    char ip[INET_ADDRSTRLEN];
    int inUse;
    int threadCount;
    int connected;
    pthread_mutex_t sendMutex;
    pthread_cond_t sendCond;
    uint8_t sendData[SEND_BUFFER_SIZE];
    size_t sendLength;
    int sendPending;
    int sendComplete;
    int sendResult;
} ClientInfo;

typedef void (*ProcessPacketData)(const ClientInfo *client,
                                  const uint8_t *data,
                                  size_t length);

typedef struct _PacketHandler
{
    CmdList cmd;
    size_t minDataLength;
    size_t maxDataLength;
    ProcessPacketData processData;
} PacketHandler;

static ClientInfo clientInfo[MAX_CLNT];
static int clientCount;
static pthread_mutex_t clientMutex = PTHREAD_MUTEX_INITIALIZER;

static int ParsePort(const char *port);
static int InitializeClients(void);
static int GetClientCount(void);
static ClientInfo *RegisterClient(int clientSocket, const struct sockaddr_in *clientAddress);
static void UnregisterClientThread(ClientInfo *client);
static void StopClient(ClientInfo *client);
static int ReceiveAll(int socketFd, void *buffer, size_t length);
static int WaitForReceiveData(int socketFd, int timeoutMs);
static int SendAll(int socketFd, const void *buffer, size_t length);
static int RequestSend(ClientInfo *client, const void *data, size_t length);
static const PacketHandler *FindPacketHandler(uint8_t cmd);
static void ProcessDhtData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length);
static void ProcessFanData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length);
static void ProcessConData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length);
static void ProcessMemData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length);
static void ProcessChatData(const ClientInfo *client,
                            const uint8_t *data,
                            size_t length);
static void *SendClient(void *arg);
static void *ReceiveClient(void *arg);
static void LogFile(const char *message);

static const PacketHandler PACKET_HANDLERS[] =
{
    {CMD_DHT11_DATA, sizeof(DhtData), sizeof(DhtData), ProcessDhtData},
    {CMD_FAN_DATA, sizeof(FanData), sizeof(FanData), ProcessFanData},
    {CMD_CON_DATA, sizeof(ConData), sizeof(ConData), ProcessConData},
    {CMD_MEM_DATA, sizeof(MemData), sizeof(MemData), ProcessMemData},
    {CMD_CHAT_DATA, 0, MAX_MESSAGE_SIZE, ProcessChatData}
};

int StartServer(const char *port)
{
    int serverSocket;
    int serverPort;
    int socketOption = 1;
    struct sockaddr_in serverAddress;

    serverPort = ParsePort(port);
    if(serverPort < 0)
    {
        fprintf(stderr, "Invalid port: %s\n", port);
        return -1;
    }

    if(InitializeClients() != 0)
    {
        fputs("client synchronization initialization failed\n", stderr);
        return -1;
    }

    serverSocket = socket(PF_INET, SOCK_STREAM, 0);
    if(serverSocket < 0)
    {
        perror("socket()");
        return -1;
    }

    if(setsockopt(serverSocket,SOL_SOCKET,SO_REUSEADDR,&socketOption,sizeof(socketOption)) < 0)
    {
        perror("setsockopt()");
        close(serverSocket);
        return -1;
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons((uint16_t)serverPort);

    if(bind(serverSocket,(struct sockaddr *)&serverAddress,sizeof(serverAddress)) < 0)
    {
        perror("bind()");
        close(serverSocket);
        return -1;
    }

    if(listen(serverSocket, LISTEN_BACKLOG) < 0)
    {
        perror("listen()");
        close(serverSocket);
        return -1;
    }

    printf("IoT server started on port %d\n", serverPort);

    while(1)
    {
        int clientSocket;
        socklen_t clientAddressSize = sizeof(struct sockaddr_in);
        struct sockaddr_in clientAddress;
        ClientInfo *client;
        pthread_t sendThread;
        pthread_t receiveThread;
        int createResult;

        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientAddressSize);
        if(clientSocket < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            perror("accept()");
            continue;
        }

        client = RegisterClient(clientSocket, &clientAddress);
        if(client == NULL)
        {
            fputs("socket full\n", stderr);
            close(clientSocket);
            continue;
        }

        createResult = pthread_create(&sendThread, NULL, SendClient, client);
        if(createResult != 0)
        {
            errno = createResult;
            perror("pthread_create(send)");
            StopClient(client);
            UnregisterClientThread(client);
            UnregisterClientThread(client);
            continue;
        }
        pthread_detach(sendThread);

        createResult = pthread_create(&receiveThread, NULL, ReceiveClient, client);
        if(createResult != 0)
        {
            errno = createResult;
            perror("pthread_create(receive)");
            StopClient(client);
            UnregisterClientThread(client);
            continue;
        }
        pthread_detach(receiveThread);

        printf("Client connected: ip=%s, fd=%d, clients=%d\n",
               client->ip,client->fd,GetClientCount());
    }
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

static int InitializeClients(void)
{
    int i;

    memset(clientInfo, 0, sizeof(clientInfo));
    for(i = 0; i < MAX_CLNT; i++)
    {
        clientInfo[i].index = i;
        clientInfo[i].fd = -1;

        if(pthread_mutex_init(&clientInfo[i].sendMutex, NULL) != 0)
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

static ClientInfo *RegisterClient(int clientSocket,
                                  const struct sockaddr_in *clientAddress)
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
            client->inUse = 1;
            client->threadCount = 2;
            client->connected = 1;
            client->sendLength = 0;
            client->sendPending = 0;
            client->sendComplete = 0;
            client->sendResult = 0;
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
    int remainingClients = 0;

    pthread_mutex_lock(&clientMutex);
    client->threadCount--;
    if(client->threadCount == 0)
    {
        socketToClose = client->fd;
        client->fd = -1;
        client->inUse = 0;
        clientCount--;
        remainingClients = clientCount;
    }
    pthread_mutex_unlock(&clientMutex);

    if(socketToClose >= 0)
    {
        close(socketToClose);
        printf("Client disconnected: ip=%s, clients=%d\n",
               client->ip,
               remainingClients);
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

static int WaitForReceiveData(int socketFd, int timeoutMs)
{
    struct pollfd socketEvent;

    socketEvent.fd = socketFd;
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

static const PacketHandler *FindPacketHandler(uint8_t cmd)
{
    size_t i;

    for(i = 0; i < sizeof(PACKET_HANDLERS) / sizeof(PACKET_HANDLERS[0]); i++)
    {
        if(PACKET_HANDLERS[i].cmd == (CmdList)cmd)
        {
            return &PACKET_HANDLERS[i];
        }
    }

    return NULL;
}

static void ProcessDhtData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length)
{
    DhtData dhtData;

    (void)length;
    memcpy(&dhtData, data, sizeof(dhtData));
    printf("[%s] DHT: temp=%u, humi=%u\n",
           client->ip,
           (unsigned int)dhtData.temp,
           (unsigned int)dhtData.humi);
}

static void ProcessFanData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length)
{
    FanData fanData;

    (void)length;
    memcpy(&fanData, data, sizeof(fanData));
    printf("[%s] FAN: fanSpeed=%u\n",
           client->ip,
           (unsigned int)fanData.fanSpeed);
}

static void ProcessConData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length)
{
    ConData conData;

    (void)length;
    memcpy(&conData, data, sizeof(conData));
    printf("[%s] CON: tempData=%u\n",
           client->ip,
           (unsigned int)conData.tempData);
}

static void ProcessMemData(const ClientInfo *client,
                           const uint8_t *data,
                           size_t length)
{
    MemData memData;

    (void)length;
    memcpy(&memData, data, sizeof(memData));
    printf("[%s] MEM: id=%.*s, pw=********\n",
           client->ip,
           MEM_ID_SIZE,
           memData.id);
}

static void ProcessChatData(const ClientInfo *client,
                            const uint8_t *data,
                            size_t length)
{
    printf("[%s] %.*s\n", client->ip, (int)length, (const char *)data);
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

        sendResult = SendAll(socketFd, sendBuffer, sendLength);

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
    uint8_t headerData[HEADER_SIZE];
    uint8_t receiveData[MAX_MESSAGE_SIZE];
    char logBuffer[BUF_SIZE];

    while(1)
    {
        HeaderData header;
        uint8_t requestHeader[HEADER_SIZE];
        const PacketHandler *packetHandler;
        int waitResult = 1;

        if(ReceiveAll(client->fd, headerData, sizeof(headerData)) != 0)
        {
            break;
        }

        header.head0 = (char)headerData[0];
        header.head1 = (char)headerData[1];
        header.cmd = headerData[2];
        header.dataLen = headerData[3];

        if(header.head0 != HEADER_OK_0 || header.head1 != HEADER_OK_1)
        {
            snprintf(logBuffer,sizeof(logBuffer),"Invalid header from %s: %c%c\n",
                    client->ip, header.head0, header.head1);
            LogFile(logBuffer);
            break;
        }

        packetHandler = FindPacketHandler(header.cmd);
        if(packetHandler == NULL)
        {
            snprintf(logBuffer,
                     sizeof(logBuffer),
                     "Unsupported command from %s: %u\n",
                     client->ip,
                     header.cmd);
            LogFile(logBuffer);
            break;
        }

        if(header.dataLen < packetHandler->minDataLength ||
           header.dataLen > packetHandler->maxDataLength)
        {
            snprintf(logBuffer,
                     sizeof(logBuffer),
                     "Invalid data length from %s: cmd=%u, length=%u\n",
                     client->ip,
                     header.cmd,
                     header.dataLen);
            LogFile(logBuffer);
            break;
        }

        requestHeader[0] = HEADER_REQUEST_0;
        requestHeader[1] = HEADER_REQUEST_1;
        requestHeader[2] = header.cmd;
        requestHeader[3] = 0;

        do
        {
            if(RequestSend(client, requestHeader, sizeof(requestHeader)) != 0)
            {
                waitResult = -1;
                break;
            }

            if(header.dataLen == 0)
            {
                break;
            }

            waitResult = WaitForReceiveData(client->fd,
                                            DATA_WAIT_TIMEOUT_MS);
            if(waitResult == 0)
            {
                snprintf(logBuffer,
                         sizeof(logBuffer),
                         "Data timeout from %s: resend RQ\n",
                         client->ip);
                LogFile(logBuffer);
            }
        }
        while(waitResult == 0);

        if(waitResult < 0 ||
           ReceiveAll(client->fd, receiveData, header.dataLen) != 0)
        {
            break;
        }

        packetHandler->processData(client, receiveData, header.dataLen);
    }

    StopClient(client);
    UnregisterClientThread(client);
    return NULL;
}

static void LogFile(const char *message)
{
    fputs(message, stdout);
}
