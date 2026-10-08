#include "ACmdWiFi.h"
#include "ACommand.h"

#include <APacket.h>
#include <AWiFi.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// wifi <command>
// ---------------------------------------------------------------------------

static AWiFi *wifi = nullptr;

void BindWiFi(AWiFi &_wifi)
{
    wifi = &_wifi;
}

static void WiFiStatus(Print &out, const char *args)
{
    (void)args;
    wifi->PrintStatus(out);
}

static void WiFiScan(Print &out, const char *args)
{
    (void)args;
    out.println(F("scanning..."));
    wifi->PrintScan(out);
}

static void WiFiConnect(Print &out, const char *args)
{
    char ssid[33];
    char pass[65];

    const char *p = NextToken(args, ssid, sizeof(ssid));
    if (p == nullptr || ssid[0] == '\0' || NextToken(p, pass, sizeof(pass)) == nullptr)
    {
        out.println(F("usage: wifi connect <ssid> [pass]  (ssid <= 32, pass <= 64)"));
        return;
    }

    out.print(F("connecting to "));
    out.println(ssid);

    out.println(wifi->Connect(ssid, pass) ? F("connected") : F("failed"));
}

static void WiFiDisconnect(Print &out, const char *args)
{
    (void)args;
    wifi->Disconnect();
    out.println(F("disconnected"));
}

static void WiFiAutoConnect(Print &out, const char *args)
{
    char token[TOKEN_SIZE];
    NextToken(args, token, TOKEN_SIZE);

    bool enable;
    if (strcasecmp_P(token, PSTR("on")) == 0)
    {
        enable = true;
    }
    else if (strcasecmp_P(token, PSTR("off")) == 0)
    {
        enable = false;
    }
    else
    {
        out.println(F("usage: wifi autoconnect <on|off>"));
        return;
    }

    out.println(wifi->SetAutoConnect(enable) ? F("ok") : F("failed"));
}

static void WiFiInit(Print &out, const char *args)
{
    long baud = strtol(args, nullptr, 10);
    if (baud <= 0)
    {
        out.println(F("usage: wifi init <baud>"));
        return;
    }

    out.println(wifi->Begin(baud) ? F("module OK") : F("module not found"));
}

static void WiFiBaud(Print &out, const char *args)
{
    long baud = strtol(args, nullptr, 10);
    if (baud <= 0)
    {
        out.println(F("usage: wifi baud <baud>"));
        return;
    }

    out.println(wifi->SetModuleBaud(baud) ? F("module OK") : F("module not found"));
}

// EEPROM에 저장된 계정(member set)으로 서버에 로그인 요청
static void WiFiLogin(Print &out, const char *args)
{
    (void)args;

    if (!wifi->IsServerConnected())
    {
        out.println(F("server not connected"));
        return;
    }

    out.println(F("login..."));

    switch (APacketLogin())
    {
    case LOGIN_SUCCESS:
        out.println(F("login success"));
        break;
    case LOGIN_REJECTED:
        out.println(F("login rejected (id/pw)"));
        break;
    case LOGIN_NO_SERVER:
        out.println(F("send failed"));
        break;
    case LOGIN_TIMEOUT:
        out.println(F("no response (timeout)"));
        break;
    case LOGIN_NO_MEMBER:
        out.println(F("no member saved (member set <id> <pw>)"));
        break;
    default:
        out.println(F("bad response packet"));
        break;
    }
}

static const ACommand wifiCommands[] = {
    {"status", WiFiStatus},
    {"scan", WiFiScan},
    {"connect", WiFiConnect},
    {"disconnect", WiFiDisconnect},
    {"autoconnect", WiFiAutoConnect},
    {"init", WiFiInit},
    {"baud", WiFiBaud},
    {"login", WiFiLogin},
};

void CmdWiFi(Print &out, const char *args)
{
    if (wifi == nullptr)
    {
        out.println(F("wifi not bound"));
        return;
    }

    Dispatch(out, args, F("wifi"), wifiCommands, sizeof(wifiCommands) / sizeof(wifiCommands[0]));
}
