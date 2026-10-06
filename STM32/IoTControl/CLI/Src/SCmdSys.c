#include "SCmdSys.h"
#include "SCommand.h"
#include "SCLI.h"
#include "SLog.h"

// ---------------------------------------------------------------------------
// sys <command>
// ---------------------------------------------------------------------------

static void SysUptime(const char *args)
{
    (void)args;
    SCLIPrintf("tick=%lu ms\r\n", (unsigned long)HAL_GetTick());
}

static void SysLog(const char *args)
{
    char *end;
    long level = strtol(args, &end, 10);
    if (end == args || level < SLOG_LEVEL_NONE || level > SLOG_LEVEL_DEBUG)
    {
        SCLIPrintf("log level=%u (compile=%u)\r\n", SLogGetLevel(), SLOG_LEVEL);
        SCLIPrintf("usage: sys log <0:none 1:error 2:warn 3:info 4:debug>\r\n");
        return;
    }

    SLogSetLevel((uint8_t)level);
    SCLIPrintf("log level=%u\r\n", SLogGetLevel());
}

static void SysReset(const char *args)
{
    (void)args;
    SCLIPrintf("reset...\r\n");
    NVIC_SystemReset();
}

static const SCommand sysCommands[] = {
    {"tick", SysUptime},
    {"log", SysLog},
    {"reset", SysReset},
};

void SCmdSys(const char *args)
{
    SCommandDispatch(args, "sys", sysCommands, SCOMMAND_COUNT(sysCommands));
}
