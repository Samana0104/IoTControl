#pragma once

#include "IoTProtocol.h"

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

typedef struct _IotClient
{
    int fd;
    SSL_CTX *tlsContext;
    SSL *tls;
} IotClient;

void InitializeClient(IotClient *client);
int ConnectClient(IotClient *client, const char *serverIp, const char *port);
int AuthenticateClient(IotClient *client, const char *memberId, const char *password);
int RegisterBluetoothDevice(IotClient *client, const char *bluetoothMac, const char *pin);
int SendChatMessage(IotClient *client, const char *message);
void DisconnectClient(IotClient *client);
