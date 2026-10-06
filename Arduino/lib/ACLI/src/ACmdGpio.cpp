#include "ACmdGpio.h"
#include "ACommand.h"

#include <stdlib.h>
#include <string.h>

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

static void GpioPins(Print &out, const char *args)
{
    (void)args;
    out.println(F("pin\tmode\tlevel"));
    for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS; ++pin)
    {
        PrintPinState(out, pin);
    }
}

static const ACommand gpioCommands[] = {
    {"mode", GpioMode},
    {"write", GpioWrite},
    {"read", GpioRead},
    {"pins", GpioPins},
};

void CmdGpio(Print &out, const char *args)
{
    Dispatch(out, args, F("gpio"), gpioCommands, sizeof(gpioCommands) / sizeof(gpioCommands[0]));
}
