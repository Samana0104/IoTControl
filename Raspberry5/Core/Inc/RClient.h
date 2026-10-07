#pragma once

#include "IoTProtocol.h"
#include "IoTPacket.h"

#define TLS_CLIENT_CONFIG_FILE "tls_client_config.txt"

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

typedef struct _IotClient
{
    int fd;
    SSL_CTX *tlsContext;
    SSL *tls;
} IotClient;

void InitializeClient(IotClient *client);
/* Reads IOT_TLS_CA_FILE from TLS_CLIENT_CONFIG_FILE in the working directory. */
int ConnectClient(IotClient *client, const char *serverIp, const char *port);
int AuthenticateClient(IotClient *client, const char *memberId, const char *password);
int RegisterBluetoothDevice(IotClient *client, const char *bluetoothMac, const char *pin);
/* Uses the existing TLS connection without opening/closing a TCP connection.
   Returns 1 for connected, 0 for server rejection/failure, -1 for local/transport error. */
int RequestBluetoothConnection(IotClient *client, const char *memberId, const char *password, const char *bluetoothMac);
int SendChatMessage(IotClient *client, const char *message);
/* Sends OK, waits for RQ, then sends exactly DHT_DATA_SIZE payload bytes. */
int SendDhtData(IotClient *client, const DhtData *data);
void DisconnectClient(IotClient *client);
