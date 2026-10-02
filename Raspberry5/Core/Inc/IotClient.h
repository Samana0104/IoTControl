#pragma once

#include "IoTProtocol.h"

typedef struct _IotClient
{
    int fd;
} IotClient;

void InitializeClient(IotClient *client);
int ConnectClient(IotClient *client, const char *serverIp, const char *port);
int SendChatMessage(IotClient *client, const char *message);
void DisconnectClient(IotClient *client);
