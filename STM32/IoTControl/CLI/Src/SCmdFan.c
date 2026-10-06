#include "SCmdFan.h"
#include "SCommand.h"
#include "SCLI.h"
#include "SFan.h"

// ---------------------------------------------------------------------------
// fan <command>
// ---------------------------------------------------------------------------

static void FanSet(const char *args)
{
    char *end;
    long percent = strtol(args, &end, 10);
    if (end == args || percent < 0 || percent > 100)
    {
        SCLIPrintf("usage: fan set <0-100>  (below %d = stop, max %d)\r\n", SFAN_MIN_PERCENT, SFAN_MAX_PERCENT);
        return;
    }

    SFanSetSpeed((uint8_t)percent);
    SCLIPrintf("fan speed=%u%%\r\n", SFanGetSpeed());
}

static void FanStop(const char *args)
{
    (void)args;
    SFanStop();
    SCLIPrintf("fan stopped\r\n");
}

static void FanStatus(const char *args)
{
    (void)args;
    SCLIPrintf("fan speed=%u%% (min %d, max %d, kick %dms)\r\n", SFanGetSpeed(), SFAN_MIN_PERCENT,
               SFAN_MAX_PERCENT, SFAN_KICK_MS);
}

static const SCommand fanCommands[] = {
    {"set", FanSet},
    {"stop", FanStop},
    {"status", FanStatus},
};

void SCmdFan(const char *args)
{
    SCommandDispatch(args, "fan", fanCommands, SCOMMAND_COUNT(fanCommands));
}
