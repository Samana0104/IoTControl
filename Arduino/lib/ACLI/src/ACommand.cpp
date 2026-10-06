#include "ACommand.h"

#include <AWiFi.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 공용 헬퍼
// ---------------------------------------------------------------------------

static constexpr uint8_t TOKEN_SIZE = 12;

using SubHandler = void (*)(Print &out, const char *args);

struct SubCommand
{
    const char *name;
    SubHandler handler;
};

// 공백 전까지 잘라서 token에 복사하고, 다음 위치를 반환
// token 크기를 넘으면 nullptr 반환
static const char *NextToken(const char *p, char *token, uint8_t size)
{
    while (*p == ' ')
    {
        ++p;
    }

    uint8_t n = 0;
    bool truncated = false;
    while (*p != '\0' && *p != ' ')
    {
        if (n < size - 1)
        {
            token[n++] = *p;
        }
        else
        {
            truncated = true;
        }
        ++p;
    }
    token[n] = '\0';
    return truncated ? nullptr : p;
}

// 첫 토큰으로 하위 커맨드를 찾아 나머지 인자를 넘김, 없으면 하위 목록 출력
static void Dispatch(Print &out, const char *args, const __FlashStringHelper *group,
                     const SubCommand *subs, uint8_t count)
{
    char token[TOKEN_SIZE];
    const char *p = NextToken(args, token, TOKEN_SIZE);

    if (p != nullptr && token[0] != '\0')
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            if (strcasecmp(token, subs[i].name) == 0)
            {
                while (*p == ' ')
                {
                    ++p;
                }
                subs[i].handler(out, p);
                return;
            }
        }
    }

    out.print(F("usage: "));
    out.print(group);
    out.println(F(" <command>"));
    for (uint8_t i = 0; i < count; ++i)
    {
        out.print(F("  "));
        out.println(subs[i].name);
    }
}

// ---------------------------------------------------------------------------
// GPIO 헬퍼
// ---------------------------------------------------------------------------

// 0, 1번은 하드웨어 Serial(CLI)이라 건드리면 CLI가 끊김
static bool IsReservedPin(uint8_t pin)
{
    return pin == 0 || pin == 1;
}

// "13" 또는 "A0" 형식
static bool ParsePin(const char *token, uint8_t &pin)
{
    const char *digits = token;
    uint8_t offset = 0;
    if (*digits == 'a' || *digits == 'A')
    {
        ++digits;
        offset = A0;
    }

    char *end;
    long value = strtol(digits, &end, 10);
    if (end == digits || *end != '\0')
    {
        return false;
    }

    value += offset;
    if (value < 0 || value >= NUM_DIGITAL_PINS)
    {
        return false;
    }

    pin = (uint8_t)value;
    return true;
}

// 첫 토큰을 핀 번호로 읽고, 실패하면 메시지 출력
static const char *ReadPinArg(Print &out, const char *args, uint8_t &pin, bool allowReserved)
{
    char token[TOKEN_SIZE];
    const char *p = NextToken(args, token, TOKEN_SIZE);

    if (p == nullptr || !ParsePin(token, pin))
    {
        out.println(F("invalid pin"));
        return nullptr;
    }

    if (!allowReserved && IsReservedPin(pin))
    {
        out.println(F("pin 0, 1 is reserved for Serial"));
        return nullptr;
    }

    return p;
}

static void PrintPinName(Print &out, uint8_t pin)
{
    if (pin >= A0)
    {
        out.print('A');
        out.print(pin - A0);
    }
    else
    {
        out.print('D');
        out.print(pin);
    }
}

// pinMode 상태는 Arduino API로 못 읽어서 레지스터를 직접 확인
// digitalRead는 PWM 핀의 PWM을 꺼버리므로 PIN 레지스터를 직접 읽음
static void PrintPinState(Print &out, uint8_t pin)
{
    uint8_t port = digitalPinToPort(pin);
    uint8_t mask = digitalPinToBitMask(pin);

    bool isOutput = (*portModeRegister(port) & mask) != 0;
    bool portBit = (*portOutputRegister(port) & mask) != 0;
    bool level = (*portInputRegister(port) & mask) != 0;

    PrintPinName(out, pin);
    out.print('\t');

    if (isOutput)
    {
        out.print(F("OUT"));
    }
    else if (portBit)
    {
        out.print(F("PULLUP"));
    }
    else
    {
        out.print(F("IN"));
    }

    out.print('\t');
    out.print(level ? F("HIGH") : F("LOW"));

    if (IsReservedPin(pin))
    {
        out.print(F("\t(Serial)"));
    }
    out.println();
}

// ---------------------------------------------------------------------------
// gpio <command>
// ---------------------------------------------------------------------------

static void GpioMode(Print &out, const char *args)
{
    uint8_t pin;
    const char *p = ReadPinArg(out, args, pin, false);
    if (p == nullptr)
    {
        out.println(F("usage: gpio mode <pin> <in|out|pullup>"));
        return;
    }

    char token[TOKEN_SIZE];
    NextToken(p, token, TOKEN_SIZE);

    if (strcasecmp_P(token, PSTR("in")) == 0)
    {
        pinMode(pin, INPUT);
    }
    else if (strcasecmp_P(token, PSTR("out")) == 0)
    {
        pinMode(pin, OUTPUT);
    }
    else if (strcasecmp_P(token, PSTR("pullup")) == 0)
    {
        pinMode(pin, INPUT_PULLUP);
    }
    else
    {
        out.println(F("usage: gpio mode <pin> <in|out|pullup>"));
        return;
    }

    PrintPinState(out, pin);
}

static void GpioWrite(Print &out, const char *args)
{
    uint8_t pin;
    const char *p = ReadPinArg(out, args, pin, false);
    if (p == nullptr)
    {
        out.println(F("usage: gpio write <pin> <0|1|low|high>"));
        return;
    }

    char token[TOKEN_SIZE];
    NextToken(p, token, TOKEN_SIZE);

    uint8_t value;
    if (strcmp_P(token, PSTR("1")) == 0 || strcasecmp_P(token, PSTR("high")) == 0)
    {
        value = HIGH;
    }
    else if (strcmp_P(token, PSTR("0")) == 0 || strcasecmp_P(token, PSTR("low")) == 0)
    {
        value = LOW;
    }
    else
    {
        out.println(F("usage: gpio write <pin> <0|1|low|high>"));
        return;
    }

    // 입력 모드에서 digitalWrite는 풀업 on/off가 되므로 알려줌
    if ((*portModeRegister(digitalPinToPort(pin)) & digitalPinToBitMask(pin)) == 0)
    {
        out.println(F("warning: pin is not OUTPUT (HIGH = pullup)"));
    }

    digitalWrite(pin, value);
    PrintPinState(out, pin);
}

static void GpioRead(Print &out, const char *args)
{
    uint8_t pin;
    if (ReadPinArg(out, args, pin, true) == nullptr)
    {
        out.println(F("usage: gpio read <pin>"));
        return;
    }

    PrintPinState(out, pin);
}

static void GpioList(Print &out, const char *args)
{
    (void)args;
    out.println(F("pin\tmode\tlevel"));
    for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS; ++pin)
    {
        PrintPinState(out, pin);
    }
}

static const SubCommand gpioCommands[] = {
    {"mode", GpioMode},
    {"write", GpioWrite},
    {"read", GpioRead},
    {"list", GpioList},
};

void CmdGpio(Print &out, const char *args)
{
    Dispatch(out, args, F("gpio"), gpioCommands, sizeof(gpioCommands) / sizeof(gpioCommands[0]));
}

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

static const SubCommand wifiCommands[] = {
    {"status", WiFiStatus},
    {"scan", WiFiScan},
    {"connect", WiFiConnect},
    {"disconnect", WiFiDisconnect},
    {"autoconnect", WiFiAutoConnect},
    {"init", WiFiInit},
    {"baud", WiFiBaud},
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
