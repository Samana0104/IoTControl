#include "ACmdSys.h"
#include "ACommand.h"

#include <ALog.h>
#include <avr/wdt.h>
#include <string.h>

// ---------------------------------------------------------------------------
// sys 헬퍼
// ---------------------------------------------------------------------------

// 힙(또는 .bss 끝)과 스택 사이의 남은 RAM
static int GetFreeRam()
{
    extern char __heap_start;
    extern char *__brkval;

    char top;
    return (int)&top - (__brkval == nullptr ? (int)&__heap_start : (int)__brkval);
}

static void PrintUptime(Print &out)
{
    uint32_t sec = millis() / 1000;

    out.print(sec / 86400);
    out.print(F("d "));
    out.print((sec / 3600) % 24);
    out.print(F("h "));
    out.print((sec / 60) % 60);
    out.print(F("m "));
    out.print(sec % 60);
    out.println('s');
}

static void PrintLogLevelName(Print &out, uint8_t level)
{
    switch (level)
    {
    case ALOG_LEVEL_NONE:
        out.println(F("none"));
        break;
    case ALOG_LEVEL_ERROR:
        out.println(F("error"));
        break;
    case ALOG_LEVEL_WARN:
        out.println(F("warn"));
        break;
    case ALOG_LEVEL_INFO:
        out.println(F("info"));
        break;
    default:
        out.println(F("debug"));
        break;
    }
}

// ---------------------------------------------------------------------------
// sys <command>
// ---------------------------------------------------------------------------

static void SysInfo(Print &out, const char *args)
{
    (void)args;

    out.print(F("build:    "));
    out.print(F(__DATE__));
    out.print(' ');
    out.println(F(__TIME__));
    out.print(F("uptime:   "));
    PrintUptime(out);
    out.print(F("free ram: "));
    out.print(GetFreeRam());
    out.println(F(" bytes"));
    out.print(F("log:      "));
    PrintLogLevelName(out, GetLogLevel());
}

static void SysMem(Print &out, const char *args)
{
    (void)args;

    out.print(F("free ram: "));
    out.print(GetFreeRam());
    out.println(F(" bytes"));
}

static void SysLog(Print &out, const char *args)
{
    char token[TOKEN_SIZE];
    NextToken(args, token, TOKEN_SIZE);

    // 인자 없으면 현재 레벨만 출력
    if (token[0] == '\0')
    {
        out.print(F("log: "));
        PrintLogLevelName(out, GetLogLevel());
        return;
    }

    uint8_t level;
    if (strcasecmp_P(token, PSTR("none")) == 0)
    {
        level = ALOG_LEVEL_NONE;
    }
    else if (strcasecmp_P(token, PSTR("error")) == 0)
    {
        level = ALOG_LEVEL_ERROR;
    }
    else if (strcasecmp_P(token, PSTR("warn")) == 0)
    {
        level = ALOG_LEVEL_WARN;
    }
    else if (strcasecmp_P(token, PSTR("info")) == 0)
    {
        level = ALOG_LEVEL_INFO;
    }
    else if (strcasecmp_P(token, PSTR("debug")) == 0)
    {
        level = ALOG_LEVEL_DEBUG;
    }
    else
    {
        out.println(F("usage: sys log [none|error|warn|info|debug]"));
        return;
    }

    // 컴파일 레벨보다 자세한 로그는 코드에 없어서 올려도 안 나옴
    if (level > ALOG_LEVEL)
    {
        out.println(F("warning: above compile level (ALOG_LEVEL)"));
    }

    SetLogLevel(level);
    out.print(F("log: "));
    PrintLogLevelName(out, level);
}

static void SysReset(Print &out, const char *args)
{
    (void)args;

    out.println(F("reset..."));
    out.flush();

    // 워치독으로 리셋
    wdt_enable(WDTO_15MS);
    while (true)
    {
    }
}

static const SubCommand sysCommands[] = {
    {"info", SysInfo},
    {"mem", SysMem},
    {"log", SysLog},
    {"reset", SysReset},
};

void CmdSys(Print &out, const char *args)
{
    Dispatch(out, args, F("sys"), sysCommands, sizeof(sysCommands) / sizeof(sysCommands[0]));
}
