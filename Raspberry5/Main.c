#include "RTCPServer.h"

#include <stdio.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

int main(void)
{
    TCPServer server;
    int result;

    if(InitServer(&server, SERVER_IP, SERVER_PORT) != 0)
    {
        return 1;
    }
    /* On failure the console stays open; fix the config and retry 'server start'. */
    if(OpenServer(&server) != 0)
    {
        puts("Server start failed. Fix the configuration and retry 'server start'.");
    }
    result = RunServer(&server);
    CloseServer(&server);

    return 0;
}
