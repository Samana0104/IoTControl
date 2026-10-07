#include "RTCPServer.h"
#include "RLog.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

int main(void)
{
    RLogBegin(stdout);
    TCPServer server;
    int result;

    RLOG_INFO("Initializing server at %s:%d.", SERVER_IP, SERVER_PORT);
    if(InitServer(&server, SERVER_IP, SERVER_PORT) != 0)
    {
        return 1;
    }
    /* On failure the console stays open; fix the config and retry 'server start'. */
    if(OpenServer(&server) != 0)
    {
        RLOG_ERROR("Server start failed. Fix the configuration and retry 'server start'.");
    }
    
    result = RunServer(&server);
    CloseServer(&server);
    
    RLOG_INFO("Server stopped with result code %d.", result);
    return result == 0 ? 0 : 1;
}
