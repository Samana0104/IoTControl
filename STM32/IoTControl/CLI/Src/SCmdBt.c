#include "SCmdBt.h"
#include "SCommand.h"
#include "SCLI.h"
#include "SZS040.h"

#define SCMDBT_VALUE_SIZE 48

// ---------------------------------------------------------------------------
// 데이터 모드
// ---------------------------------------------------------------------------

static void BtStatus(const char *args)
{
    (void)args;
    SCLIPrintf("connected=%s (STATE pin), mcu uart baud=%lu\r\n", SZS040IsConnected() ? "yes" : "no",
               (unsigned long)SZS040GetUartBaud());
}

// MCU 쪽 UART 보레이트만 바꿈 (모듈 보레이트를 이미 알고 있을 때 맞추는 용도)
static void BtUart(const char *args)
{
    char *end;
    unsigned long baud = strtoul(args, &end, 10);
    if (end == args || baud == 0)
    {
        SCLIPrintf("mcu uart baud=%lu\r\n", (unsigned long)SZS040GetUartBaud());
        SCLIPrintf("usage: bt uart <baud>\r\n");
        return;
    }

    SCLIPrintf(SZS040SetUartBaud((uint32_t)baud) ? "OK\r\n" : "failed\r\n");
}

static void BtSend(const char *args)
{
    if (args[0] == '\0')
    {
        SCLIPrintf("usage: bt send <text>\r\n");
        return;
    }

    SZS040Printf("%s\r\n", args);
    SCLIPrintf("sent\r\n");
}

// ---------------------------------------------------------------------------
// AT 모드 (페어링 안 된 상태에서만)
// ---------------------------------------------------------------------------

static void BtAt(const char *args)
{
    if (args[0] == '\0')
    {
        SCLIPrintf("usage: bt at <AT command>  (ex: bt at AT+VERSION)\r\n");
        return;
    }

    char response[SCMDBT_VALUE_SIZE];
    bool ok = SZS040SendAT(args, response, sizeof(response));

    SCLIPrintf("response=\"%s\"\r\n", response);
    SCLIPrintf(ok ? "OK\r\n" : "failed\r\n");
}

static void BtTest(const char *args)
{
    (void)args;
    SCLIPrintf(SZS040Test() ? "OK\r\n" : "failed\r\n");
}

static void BtVersion(const char *args)
{
    (void)args;
    char value[SCMDBT_VALUE_SIZE];
    if (SZS040GetVersion(value, sizeof(value)))
    {
        SCLIPrintf("version=%s\r\n", value);
        return;
    }
    SCLIPrintf("failed\r\n");
}

static void BtName(const char *args)
{
    if (args[0] == '\0')
    {
        SCLIPrintf("usage: bt name <name>  (max 20, HC-06 cannot read name)\r\n");
        return;
    }

    SCLIPrintf(SZS040SetName(args) ? "OK\r\n" : "failed\r\n");
}

static void BtPin(const char *args)
{
    if (args[0] == '\0')
    {
        SCLIPrintf("usage: bt pin <4 digits>\r\n");
        return;
    }

    SCLIPrintf(SZS040SetPin(args) ? "OK\r\n" : "failed\r\n");
}

static void BtBaud(const char *args)
{
    char *end;
    unsigned long baud = strtoul(args, &end, 10);
    if (end == args || baud == 0)
    {
        SCLIPrintf("usage: bt baud <1200|2400|4800|9600|19200|38400|57600|115200>\r\n");
        return;
    }

    if (!SZS040SetBaud((uint32_t)baud))
    {
        SCLIPrintf("failed\r\n");
        return;
    }

    SCLIPrintf("module + mcu uart baud=%lu\r\n", baud);
}

static const SCommand btCommands[] = {
    {"status", BtStatus},
    {"uart", BtUart},
    {"send", BtSend},
    {"at", BtAt},
    {"test", BtTest},
    {"version", BtVersion},
    {"name", BtName},
    {"pin", BtPin},
    {"baud", BtBaud},
};

void SCmdBt(const char *args)
{
    SCommandDispatch(args, "bt", btCommands, SCOMMAND_COUNT(btCommands));
}
