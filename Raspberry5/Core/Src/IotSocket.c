#include "IotSocket.h"
#include "IotBluetooth.h"
#include "IotDatabase.h"
#include "IoTPacket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define LISTEN_BACKLOG 5
#define SEND_BUFFER_SIZE (MAX_MESSAGE_SIZE + HEADER_SIZE)
#define DATA_WAIT_TIMEOUT_MS 5000
#define TLS_HANDSHAKE_TIMEOUT_SECONDS 5
#define BLUETOOTH_CONNECT_TIMEOUT_MS 5000
#define BLUETOOTH_PAIR_TIMEOUT_SECONDS 30

typedef struct _ClientInfo
{
    int index;
    int fd;
    SSL *tls;
    char ip[INET_ADDRSTRLEN];
    char memberId[MEM_ID_SIZE + 1];
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    int bluetoothFd;
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

typedef int (*ProcessPacketData)(ClientInfo *client, const uint8_t *data, size_t length);

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
static pthread_mutex_t bluetoothPairMutex = PTHREAD_MUTEX_INITIALIZER;

static int ParsePort(const char *port);
static int SetSocketTimeout(int socketFd, int timeoutSeconds);
static int InitializeClients(void);
static int GetClientCount(void);
static ClientInfo *RegisterClient(int clientSocket, SSL *tls, const struct sockaddr_in *clientAddress);
static void UnregisterClientThread(ClientInfo *client);
static void StopClient(ClientInfo *client);
static SSL_CTX *CreateTlsServerContext(void);
static int ReceiveAll(ClientInfo *client, void *buffer, size_t length);
static int WaitForReceiveData(ClientInfo *client, int timeoutMs);
static int SendAll(ClientInfo *client, const void *buffer, size_t length);
static int RequestSend(ClientInfo *client, const void *data, size_t length);
static const PacketHandler *FindPacketHandler(uint8_t cmd);
static int ProcessDhtData(ClientInfo *client, const uint8_t *data, size_t length);
static int ProcessFanData(ClientInfo *client, const uint8_t *data, size_t length);
static int ProcessConData(ClientInfo *client, const uint8_t *data, size_t length);
static int ProcessMemData(ClientInfo *client, const uint8_t *data, size_t length);
static int ProcessChatData(ClientInfo *client, const uint8_t *data, size_t length);
static int ProcessBluetoothRegisterData(ClientInfo *client, const uint8_t *data, size_t length);
static int ConnectMemberBluetoothDevice(ClientInfo *client);
static void *SendClient(void *arg);
static void *ReceiveClient(void *arg);
static void LogFile(const char *message);

static const PacketHandler PACKET_HANDLERS[] =
{
    {CMD_DHT11_DATA, sizeof(DhtData), sizeof(DhtData), ProcessDhtData},
    {CMD_FAN_DATA, sizeof(FanData), sizeof(FanData), ProcessFanData},
    {CMD_CON_DATA, sizeof(ConData), sizeof(ConData), ProcessConData},
    {CMD_MEM_DATA, sizeof(MemData), sizeof(MemData), ProcessMemData},
    {CMD_CHAT_DATA, 0, MAX_MESSAGE_SIZE, ProcessChatData},
    {CMD_BLUETOOTH_REGISTER, sizeof(BluetoothRegisterData), sizeof(BluetoothRegisterData), ProcessBluetoothRegisterData}
};

int StartServer(const char *port)
{
    int serverSocket;
    int serverPort;
    int socketOption = 1;
    struct sockaddr_in serverAddress;
    SSL_CTX *tlsContext;

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

    if(InitializeDatabase() != 0)
    {
        return -1;
    }

    tlsContext = CreateTlsServerContext();
    if(tlsContext == NULL)
    {
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);

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
        SSL *tls;
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

        if(SetSocketTimeout(clientSocket, TLS_HANDSHAKE_TIMEOUT_SECONDS) != 0)
        {
            perror("setsockopt(client timeout)");
            close(clientSocket);
            continue;
        }

        tls = SSL_new(tlsContext);
        if(tls == NULL || SSL_set_fd(tls, clientSocket) != 1 || SSL_accept(tls) != 1)
        {
            fputs("TLS handshake failed\n", stderr);
            ERR_print_errors_fp(stderr);
            SSL_free(tls);
            close(clientSocket);
            continue;
        }

        if(SetSocketTimeout(clientSocket, 0) != 0)
        {
            perror("setsockopt(client timeout reset)");
            SSL_shutdown(tls);
            SSL_free(tls);
            close(clientSocket);
            continue;
        }

        client = RegisterClient(clientSocket, tls, &clientAddress);
        if(client == NULL)
        {
            fputs("socket full\n", stderr);
            SSL_shutdown(tls);
            SSL_free(tls);
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

        printf("Client connected: ip=%s, fd=%d, clients=%d\n", client->ip,client->fd,GetClientCount());
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
    for(i = 0; i < MAX_CLNT; i++)
    {
        clientInfo[i].index = i;
        clientInfo[i].fd = -1;
        clientInfo[i].bluetoothFd = -1;

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
    SSL *tlsToFree = NULL;
    int remainingClients = 0;

    pthread_mutex_lock(&clientMutex);
    client->threadCount--;
    if(client->threadCount == 0)
    {
        socketToClose = client->fd;
        bluetoothToClose = client->bluetoothFd;
        tlsToFree = client->tls;
        client->fd = -1;
        client->bluetoothFd = -1;
        client->tls = NULL;
        client->inUse = 0;
        clientCount--;
        remainingClients = clientCount;
    }
    pthread_mutex_unlock(&clientMutex);

    if(socketToClose >= 0)
    {
        DisconnectBluetoothDevice(bluetoothToClose);
        SSL_free(tlsToFree);
        close(socketToClose);
        printf("Client disconnected: ip=%s, clients=%d\n", client->ip, remainingClients);
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

static SSL_CTX *CreateTlsServerContext(void)
{
    const char *certificateFile = getenv("IOT_TLS_CERT_FILE");
    const char *privateKeyFile = getenv("IOT_TLS_KEY_FILE");
    SSL_CTX *tlsContext;

    if(certificateFile == NULL || privateKeyFile == NULL)
    {
        fputs("IOT_TLS_CERT_FILE and IOT_TLS_KEY_FILE must be set\n", stderr);
        return NULL;
    }

    tlsContext = SSL_CTX_new(TLS_server_method());
    if(tlsContext == NULL)
    {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    if(SSL_CTX_set_min_proto_version(tlsContext, TLS1_2_VERSION) != 1 || SSL_CTX_use_certificate_chain_file(tlsContext, certificateFile) != 1 || SSL_CTX_use_PrivateKey_file(tlsContext, privateKeyFile, SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(tlsContext) != 1)
    {
        fputs("TLS certificate initialization failed\n", stderr);
        ERR_print_errors_fp(stderr);
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

static int ProcessDhtData(ClientInfo *client, const uint8_t *data, size_t length)
{
    DhtData dhtData;

    (void)length;
    memcpy(&dhtData, data, sizeof(dhtData));
    printf("[%s] DHT: temp=%u, humi=%u\n", client->ip, (unsigned int)dhtData.temp, (unsigned int)dhtData.humi);
    return 0;
}

static int ProcessFanData(ClientInfo *client, const uint8_t *data, size_t length)
{
    FanData fanData;

    (void)length;
    memcpy(&fanData, data, sizeof(fanData));
    printf("[%s] FAN: fanSpeed=%u\n", client->ip, (unsigned int)fanData.fanSpeed);
    return 0;
}

static int ProcessConData(ClientInfo *client, const uint8_t *data, size_t length)
{
    ConData conData;

    (void)length;
    memcpy(&conData, data, sizeof(conData));
    printf("[%s] CON: tempData=%u\n", client->ip, (unsigned int)conData.tempData);
    return 0;
}

static int ProcessMemData(ClientInfo *client, const uint8_t *data, size_t length)
{
    MemData memData;
    size_t memberIdLength;
    size_t passwordLength;
    int verifyResult;
    int bluetoothResult;

    (void)length;
    memcpy(&memData, data, sizeof(memData));
    memberIdLength = strnlen(memData.id, MEM_ID_SIZE);
    passwordLength = strnlen(memData.pw, MEM_PW_SIZE);
    verifyResult = VerifyMember(memData.id, memberIdLength, memData.pw, passwordLength);

    if(verifyResult == 1)
    {
        memcpy(client->memberId, memData.id, memberIdLength);
        client->memberId[memberIdLength] = '\0';
        bluetoothResult = ConnectMemberBluetoothDevice(client);
        verifyResult = bluetoothResult >= 0 ? 1 : 0;
        client->authenticated = verifyResult == 1;
    }

    sodium_memzero(&memData, sizeof(memData));
    if(verifyResult != 1)
    {
        DisconnectBluetoothDevice(client->bluetoothFd);
        client->bluetoothFd = -1;
        memset(client->bluetoothMac, 0, sizeof(client->bluetoothMac));
        client->memberId[0] = '\0';
        client->authenticated = 0;
        printf("[%s] Member authentication failed\n", client->ip);
        return -1;
    }

    printf("[%s] Member authenticated: id=%s\n", client->ip, client->memberId);
    return 0;
}

static int ProcessBluetoothRegisterData(ClientInfo *client, const uint8_t *data, size_t length)
{
    BluetoothRegisterData registerData;
    BluetoothDeviceRecord existingDevice;
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    char pin[BLUETOOTH_PIN_SIZE + 1];
    size_t macLength;
    size_t pinLength;
    int queryResult;
    int registerResult;
    int processResult = -1;

    (void)length;
    memcpy(&registerData, data, sizeof(registerData));
    pthread_mutex_lock(&bluetoothPairMutex);
    macLength = strnlen(registerData.mac, BLUETOOTH_MAC_SIZE);
    pinLength = strnlen(registerData.pin, BLUETOOTH_PIN_SIZE);
    if(macLength != BLUETOOTH_MAC_SIZE || pinLength == 0)
    {
        fprintf(stderr, "[%s] Invalid HC-05 registration data: id=%s\n", client->ip, client->memberId);
        goto cleanup;
    }

    memcpy(bluetoothMac, registerData.mac, macLength);
    bluetoothMac[macLength] = '\0';
    memcpy(pin, registerData.pin, pinLength);
    pin[pinLength] = '\0';

    queryResult = GetMemberBluetoothDevice(client->memberId, strlen(client->memberId), &existingDevice);
    if(queryResult != 0)
    {
        fprintf(stderr, "[%s] HC-05 registration rejected: id=%s, reason=%s\n", client->ip, client->memberId, queryResult > 0 ? "already registered" : "database error");
        goto cleanup;
    }

    if(PairBluetoothDevice(bluetoothMac, pin, BLUETOOTH_PAIR_TIMEOUT_SECONDS) != 0)
    {
        fprintf(stderr, "[%s] HC-05 pairing failed: id=%s, mac=%s: %s\n", client->ip, client->memberId, bluetoothMac, strerror(errno));
        goto cleanup;
    }

    registerResult = RegisterMemberBluetoothDevice(client->memberId, strlen(client->memberId), bluetoothMac, macLength);
    if(registerResult != 1)
    {
        fprintf(stderr, "[%s] HC-05 database registration failed: id=%s, reason=%s\n", client->ip, client->memberId, registerResult == 0 ? "already registered" : "database error");
        goto cleanup;
    }

    if(ConnectMemberBluetoothDevice(client) != 0)
    {
        goto cleanup;
    }

    printf("[%s] HC-05 registered: id=%s, mac=%s\n", client->ip, client->memberId, bluetoothMac);
    processResult = 0;

cleanup:
    pthread_mutex_unlock(&bluetoothPairMutex);
    sodium_memzero(pin, sizeof(pin));
    sodium_memzero(&registerData, sizeof(registerData));
    return processResult;
}

static int ConnectMemberBluetoothDevice(ClientInfo *client)
{
    BluetoothDeviceRecord deviceRecord;
    uint8_t rfcommChannel;
    int queryResult;
    int bluetoothFd;

    queryResult = GetMemberBluetoothDevice(client->memberId, strlen(client->memberId), &deviceRecord);
    if(queryResult == 0)
    {
        printf("[%s] HC-05 registration required: id=%s\n", client->ip, client->memberId);
        return 1;
    }
    if(queryResult < 0)
    {
        fprintf(stderr, "[%s] HC-05 database lookup failed: id=%s\n", client->ip, client->memberId);
        return -1;
    }

    bluetoothFd = ConnectBluetoothDevice(deviceRecord.mac, BLUETOOTH_CONNECT_TIMEOUT_MS, &rfcommChannel);
    if(bluetoothFd < 0)
    {
        fprintf(stderr, "[%s] HC-05 connection failed: id=%s, mac=%s: %s\n", client->ip, client->memberId, deviceRecord.mac, strerror(errno));
        return -1;
    }

    DisconnectBluetoothDevice(client->bluetoothFd);
    client->bluetoothFd = bluetoothFd;
    memcpy(client->bluetoothMac, deviceRecord.mac, sizeof(client->bluetoothMac));
    printf("[%s] HC-05 connected: id=%s, mac=%s, channel=%u\n", client->ip, client->memberId, client->bluetoothMac, (unsigned int)rfcommChannel);
    return 0;
}

static int ProcessChatData(ClientInfo *client, const uint8_t *data, size_t length)
{
    printf("[%s] %.*s\n", client->ip, (int)length, (const char *)data);
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
    uint8_t headerData[HEADER_SIZE];
    uint8_t receiveData[MAX_MESSAGE_SIZE];
    char logBuffer[BUF_SIZE];

    while(1)
    {
        HeaderData header;
        uint8_t requestHeader[HEADER_SIZE];
        const PacketHandler *packetHandler;
        int waitResult = 1;

        if(ReceiveAll(client, headerData, sizeof(headerData)) != 0)
        {
            break;
        }

        header.head0 = (char)headerData[0];
        header.head1 = (char)headerData[1];
        header.cmd = headerData[2];
        header.dataLen = headerData[3];

        if(header.head0 != HEADER_OK_0 || header.head1 != HEADER_OK_1)
        {
            snprintf(logBuffer, sizeof(logBuffer), "Invalid header from %s: %c%c\n", client->ip, header.head0, header.head1);
            LogFile(logBuffer);
            break;
        }

        packetHandler = FindPacketHandler(header.cmd);
        if(packetHandler == NULL)
        {
            snprintf(logBuffer, sizeof(logBuffer), "Unsupported command from %s: %u\n", client->ip, header.cmd);
            LogFile(logBuffer);
            break;
        }

        if(header.dataLen < packetHandler->minDataLength ||
           header.dataLen > packetHandler->maxDataLength)
        {
            snprintf(logBuffer, sizeof(logBuffer), "Invalid data length from %s: cmd=%u, length=%u\n", client->ip, header.cmd, header.dataLen);
            LogFile(logBuffer);
            break;
        }

        if(header.cmd != CMD_MEM_DATA && !client->authenticated)
        {
            snprintf(logBuffer, sizeof(logBuffer), "Unauthenticated command from %s: %u\n", client->ip, header.cmd);
            LogFile(logBuffer);
            break;
        }
        if(header.cmd != CMD_MEM_DATA && header.cmd != CMD_BLUETOOTH_REGISTER && client->bluetoothFd < 0)
        {
            snprintf(logBuffer, sizeof(logBuffer), "Bluetooth registration required from %s: cmd=%u\n", client->ip, header.cmd);
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

            waitResult = WaitForReceiveData(client, DATA_WAIT_TIMEOUT_MS);
            if(waitResult == 0)
            {
                snprintf(logBuffer, sizeof(logBuffer), "Data timeout from %s: resend RQ\n", client->ip);
                LogFile(logBuffer);
            }
        }
        while(waitResult == 0);

        if(waitResult < 0 || ReceiveAll(client, receiveData, header.dataLen) != 0)
        {
            break;
        }

        {
            int processResult = packetHandler->processData(client, receiveData, header.dataLen);

            if(header.cmd == CMD_MEM_DATA || header.cmd == CMD_BLUETOOTH_REGISTER)
            {
                sodium_memzero(receiveData, header.dataLen);
            }
            if(processResult != 0)
            {
                break;
            }
        }
    }

    StopClient(client);
    UnregisterClientThread(client);
    return NULL;
}

static void LogFile(const char *message)
{
    fputs(message, stdout);
}
