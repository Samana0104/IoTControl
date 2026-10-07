#include "RCmdFan.h"
#include "RCtrlFan.h"

#include <stdio.h>
#include <stdlib.h>

static void FanSet(TCPServer *server, const char *args)
{
    char *end;
    long fd = strtol(args, &end, 10);
    const char *percentText = RCommandSkipSpace(end);
    long percent;
    int result;

    if(end == args || percentText == end)
    {
        puts("Usage: fan set <session fd> <0..100>");
        return;
    }
    percent = strtol(percentText, &end, 10);
    if(end == percentText || *RCommandSkipSpace(end) != '\0' || fd < 0 || fd > 65535 || percent < 0 || percent > 100)
    {
        puts("Usage: fan set <session fd> <0..100>");
        return;
    }
    if(!IsServerRunning(server))
    {
        puts("Start the server first: server start [port]");
        return;
    }

    result = RCtrlFanSetSpeed((int)fd, (uint8_t)percent);
    printf("Fan control: fd=%ld, speed=%ld%%, result=%s\n", fd, percent, result == 0 ? "sent (see log for ACK)" : result == 1 ? "no connected session" : "failed");
}

static const RCommand FAN_COMMANDS[] =
{
    {"set", FanSet}
};

void RCmdFan(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "fan", FAN_COMMANDS, RCOMMAND_COUNT(FAN_COMMANDS));
}
