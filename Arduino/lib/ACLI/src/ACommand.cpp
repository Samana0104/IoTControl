#include "ACommand.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// GPIO 공용 헬퍼
// ---------------------------------------------------------------------------

static constexpr uint8_t TOKEN_SIZE = 8;

// 0, 1번은 하드웨어 Serial(CLI)이라 건드리면 CLI가 끊김
static bool IsReservedPin(uint8_t pin)
{
    return pin == 0 || pin == 1;
}

// 공백 전까지 잘라서 token에 복사하고, 다음 위치를 반환
static const char *NextToken(const char *p, char *token)
{
    while (*p == ' ')
    {
        ++p;
    }

    uint8_t n = 0;
    while (*p != '\0' && *p != ' ')
    {
        if (n < TOKEN_SIZE - 1)
        {
            token[n++] = *p;
        }
        ++p;
    }
    token[n] = '\0';
    return p;
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
    const char *p = NextToken(args, token);

    if (!ParsePin(token, pin))
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
// GPIO 커맨드
// ---------------------------------------------------------------------------

void CmdPinMode(Print &out, const char *args)
{
    uint8_t pin;
    const char *p = ReadPinArg(out, args, pin, false);
    if (p == nullptr)
    {
        out.println(F("usage: pinmode <pin> <in|out|pullup>"));
        return;
    }

    char token[TOKEN_SIZE];
    NextToken(p, token);

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
        out.println(F("usage: pinmode <pin> <in|out|pullup>"));
        return;
    }

    PrintPinState(out, pin);
}

void CmdWrite(Print &out, const char *args)
{
    uint8_t pin;
    const char *p = ReadPinArg(out, args, pin, false);
    if (p == nullptr)
    {
        out.println(F("usage: write <pin> <0|1|low|high>"));
        return;
    }

    char token[TOKEN_SIZE];
    NextToken(p, token);

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
        out.println(F("usage: write <pin> <0|1|low|high>"));
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

void CmdRead(Print &out, const char *args)
{
    uint8_t pin;
    if (ReadPinArg(out, args, pin, true) == nullptr)
    {
        out.println(F("usage: read <pin>"));
        return;
    }

    PrintPinState(out, pin);
}

void CmdPins(Print &out, const char *args)
{
    (void)args;
    out.println(F("pin\tmode\tlevel"));
    for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS; ++pin)
    {
        PrintPinState(out, pin);
    }
}
