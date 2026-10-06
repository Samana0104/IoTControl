#include "SCmdGpio.h"
#include "SCommand.h"
#include "SCLI.h"

// ---------------------------------------------------------------------------
// GPIO 헬퍼
// ---------------------------------------------------------------------------

typedef struct
{
    GPIO_TypeDef *port;
    char name;
    uint8_t number;
} SPin;

// 클럭이 켜진 포트만 (gpio.c의 MX_GPIO_Init 기준)
static GPIO_TypeDef *PortFromChar(char c)
{
    switch (c)
    {
    case 'A':
    case 'a':
        return GPIOA;
    case 'B':
    case 'b':
        return GPIOB;
    case 'C':
    case 'c':
        return GPIOC;
    case 'H':
    case 'h':
        return GPIOH;
    default:
        return NULL;
    }
}

// PA2/PA3은 USART2(CLI), PA13/PA14는 SWD라 건드리면 CLI나 디버거가 끊김
static bool IsReservedPin(const SPin *pin)
{
    return pin->port == GPIOA && (pin->number == 2 || pin->number == 3 || pin->number == 13 || pin->number == 14);
}

// "A5", "C13" 형식
static bool ParsePin(const char *token, SPin *pin)
{
    pin->port = PortFromChar(token[0]);
    if (pin->port == NULL)
    {
        return false;
    }

    const char *digits = token + 1;
    char *end;
    long value = strtol(digits, &end, 10);
    if (end == digits || *end != '\0' || value < 0 || value > 15)
    {
        return false;
    }

    pin->name = (char)(token[0] & ~0x20);
    pin->number = (uint8_t)value;
    return true;
}

// 첫 토큰을 핀으로 읽고, 실패하면 메시지 출력 후 NULL
static const char *ReadPinArg(const char *args, SPin *pin, bool allowReserved)
{
    char token[SCOMMAND_TOKEN_SIZE];
    const char *p = SCommandNextToken(args, token, SCOMMAND_TOKEN_SIZE);

    if (p == NULL || !ParsePin(token, pin))
    {
        SCLIPrintf("invalid pin (ex: A5, C13)\r\n");
        return NULL;
    }

    if (!allowReserved && IsReservedPin(pin))
    {
        SCLIPrintf("P%c%u is reserved (USART2/SWD)\r\n", pin->name, pin->number);
        return NULL;
    }

    return p;
}

// HAL로는 현재 모드를 못 읽어서 레지스터를 직접 확인
static void PrintPinState(const SPin *pin)
{
    static const char *const modeNames[] = {"IN", "OUT", "AF", "ANALOG"};
    static const char *const pullNames[] = {"", " PULLUP", " PULLDOWN", ""};

    uint32_t shift2 = pin->number * 2U;
    uint32_t mode = (pin->port->MODER >> shift2) & 0x3U;
    uint32_t pull = (pin->port->PUPDR >> shift2) & 0x3U;
    GPIO_PinState level = HAL_GPIO_ReadPin(pin->port, (uint16_t)(1U << pin->number));

    SCLIPrintf("P%c%u\t%s%s\t%s%s\r\n", pin->name, pin->number, modeNames[mode], pullNames[pull],
               level == GPIO_PIN_SET ? "HIGH" : "LOW", IsReservedPin(pin) ? "\t(reserved)" : "");
}

// ---------------------------------------------------------------------------
// gpio <command>
// ---------------------------------------------------------------------------

static void GpioMode(const char *args)
{
    SPin pin;
    const char *p = ReadPinArg(args, &pin, false);
    if (p == NULL)
    {
        SCLIPrintf("usage: gpio mode <pin> <in|out|pullup|pulldown>\r\n");
        return;
    }

    char token[SCOMMAND_TOKEN_SIZE];
    SCommandNextToken(p, token, SCOMMAND_TOKEN_SIZE);

    GPIO_InitTypeDef init = {0};
    init.Pin = 1U << pin.number;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    if (strcasecmp(token, "in") == 0)
    {
        init.Mode = GPIO_MODE_INPUT;
        init.Pull = GPIO_NOPULL;
    }
    else if (strcasecmp(token, "out") == 0)
    {
        init.Mode = GPIO_MODE_OUTPUT_PP;
        init.Pull = GPIO_NOPULL;
    }
    else if (strcasecmp(token, "pullup") == 0)
    {
        init.Mode = GPIO_MODE_INPUT;
        init.Pull = GPIO_PULLUP;
    }
    else if (strcasecmp(token, "pulldown") == 0)
    {
        init.Mode = GPIO_MODE_INPUT;
        init.Pull = GPIO_PULLDOWN;
    }
    else
    {
        SCLIPrintf("usage: gpio mode <pin> <in|out|pullup|pulldown>\r\n");
        return;
    }

    HAL_GPIO_Init(pin.port, &init);
    PrintPinState(&pin);
}

static void GpioWrite(const char *args)
{
    SPin pin;
    const char *p = ReadPinArg(args, &pin, false);
    if (p == NULL)
    {
        SCLIPrintf("usage: gpio write <pin> <0|1|low|high>\r\n");
        return;
    }

    char token[SCOMMAND_TOKEN_SIZE];
    SCommandNextToken(p, token, SCOMMAND_TOKEN_SIZE);

    GPIO_PinState value;
    if (strcmp(token, "1") == 0 || strcasecmp(token, "high") == 0)
    {
        value = GPIO_PIN_SET;
    }
    else if (strcmp(token, "0") == 0 || strcasecmp(token, "low") == 0)
    {
        value = GPIO_PIN_RESET;
    }
    else
    {
        SCLIPrintf("usage: gpio write <pin> <0|1|low|high>\r\n");
        return;
    }

    // 출력 모드가 아니면 ODR만 바뀌고 핀 레벨은 그대로라 알려줌
    if (((pin.port->MODER >> (pin.number * 2U)) & 0x3U) != 0x1U)
    {
        SCLIPrintf("warning: pin is not OUTPUT\r\n");
    }

    HAL_GPIO_WritePin(pin.port, (uint16_t)(1U << pin.number), value);
    PrintPinState(&pin);
}

static void GpioToggle(const char *args)
{
    SPin pin;
    if (ReadPinArg(args, &pin, false) == NULL)
    {
        SCLIPrintf("usage: gpio toggle <pin>\r\n");
        return;
    }

    HAL_GPIO_TogglePin(pin.port, (uint16_t)(1U << pin.number));
    PrintPinState(&pin);
}

static void GpioRead(const char *args)
{
    SPin pin;
    if (ReadPinArg(args, &pin, true) == NULL)
    {
        SCLIPrintf("usage: gpio read <pin>\r\n");
        return;
    }

    PrintPinState(&pin);
}

// 포트별 핀 개수 (F411 64핀 기준, PH는 PH0/PH1만 있음)
static uint8_t PortPinCount(char name)
{
    return name == 'H' ? 2 : 16;
}

static void PrintPortPins(char name)
{
    SPin pin;
    pin.port = PortFromChar(name);
    pin.name = name;

    for (uint8_t number = 0; number < PortPinCount(name); ++number)
    {
        pin.number = number;
        PrintPinState(&pin);
    }
}

static void GpioPins(const char *args)
{
    static const char portNames[] = {'A', 'B', 'C', 'H'};

    char token[SCOMMAND_TOKEN_SIZE];
    SCommandNextToken(args, token, SCOMMAND_TOKEN_SIZE);

    // 인자 없으면 전체, 있으면 해당 포트만
    if (token[0] == '\0')
    {
        SCLIPrintf("pin\tmode\tlevel\r\n");
        for (uint8_t i = 0; i < sizeof(portNames); ++i)
        {
            PrintPortPins(portNames[i]);
        }
        return;
    }

    if (token[1] != '\0' || PortFromChar(token[0]) == NULL)
    {
        SCLIPrintf("usage: gpio pins [A|B|C|H]\r\n");
        return;
    }

    SCLIPrintf("pin\tmode\tlevel\r\n");
    PrintPortPins((char)(token[0] & ~0x20));
}

static const SSubCommand gpioCommands[] = {
    {"mode", GpioMode},
    {"write", GpioWrite},
    {"toggle", GpioToggle},
    {"read", GpioRead},
    {"pins", GpioPins},
};

void SCmdGpio(const char *args)
{
    SCommandDispatch(args, "gpio", gpioCommands, SCOMMAND_COUNT(gpioCommands));
}
