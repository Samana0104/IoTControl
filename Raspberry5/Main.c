#include "RConfig.h"
#include "RLog.h"
#include "RTCPServer.h"

int main(void)
{
    TCPServer server;
    const RServerConfig *serverConfig;
    int result;

    RLogBegin(stdout);
    if(RConfigLoad() != 0)
    {
        return 1;
    }
    serverConfig = &RConfigGet()->server;

    RLOG_INFO("Initializing server at %s:%d.", serverConfig->ip, serverConfig->port);
    if(InitServer(&server, serverConfig->ip, serverConfig->port) != 0)
    {
        return 1;
    }
    /* On failure the console stays open; fix ServerConfig.json and retry 'server start'. */
    if(OpenServer(&server) != 0)
    {
        RLOG_ERROR("Server start failed. Fix the configuration and retry 'server start'.");
    }

    result = RunServer(&server);
    CloseServer(&server);

    RLOG_INFO("Server stopped with result code %d.", result);
    return result == 0 ? 0 : 1;
}
