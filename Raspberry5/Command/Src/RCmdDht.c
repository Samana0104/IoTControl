#include "RCmdDht.h"
#include "RPacketDht.h"

#include <stdio.h>

// PC의 REQ_DHT_COLLECT와 같은 동작. 측정값은 장치의 ACK_DHT로 도착해 DB에 기록됨
static void DhtCollect(TCPServer *server, const char *args)
{
    int sentCount;

    (void)args;
    if(server->socket < 0)
    {
        puts("Start the server first: server start [port]");
        return;
    }
    sentCount = RPacketDhtRequestAll(-1);
    if(sentCount < 0)
    {
        puts("DHT collect failed (see log)");
        return;
    }
    printf("DHT collect: sent to %d device(s) (see log for ACK_DHT)\n", sentCount);
}

static const RCommand DHT_COMMANDS[] =
{
    {"collect", DhtCollect}
};

void RCmdDht(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "dht", DHT_COMMANDS, RCOMMAND_COUNT(DHT_COMMANDS));
}
