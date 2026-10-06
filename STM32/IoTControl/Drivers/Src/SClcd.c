#include "SClcd.h"

#define SCLCD_RS_MASK 0x01U
#define SCLCD_ENABLE_MASK 0x04U
#define SCLCD_BACKLIGHT_MASK 0x08U
#define SCLCD_DATA_MASK 0xF0U
#define SCLCD_I2C_TIMEOUT_MS 100U
#define SCLCD_POWER_ON_MS 50U

#define SCLCD_CLEAR_COMMAND 0x01U
#define SCLCD_HOME_COMMAND 0x02U
#define SCLCD_ENTRY_MODE_COMMAND 0x06U
#define SCLCD_DISPLAY_COMMAND 0x08U
#define SCLCD_DISPLAY_ON_MASK 0x04U
#define SCLCD_FUNCTION_COMMAND 0x28U
#define SCLCD_DDRAM_COMMAND 0x80U
#define SCLCD_SECOND_ROW_ADDRESS 0x40U

typedef struct _SClcd
{
    I2C_HandleTypeDef *hi2c;
    uint16_t address;
    uint8_t backlightMask;
    uint8_t portValue;
    bool initialized;
} SClcd;

static SClcd lcd;

static bool WritePort(uint8_t value)
{
    if (HAL_I2C_Master_Transmit(lcd.hi2c, lcd.address, &value, 1, SCLCD_I2C_TIMEOUT_MS) != HAL_OK)
    {
        lcd.initialized = false;
        return false;
    }
    lcd.portValue = value;
    return true;
}

static bool WriteNibble(uint8_t value, bool dataMode)
{
    // RW는 항상 LOW. EN을 LOW -> HIGH -> LOW로 바꾸며 하강 에지에 래치.
    uint8_t port = (value & SCLCD_DATA_MASK) | lcd.backlightMask;
    if (dataMode)
    {
        port |= SCLCD_RS_MASK;
    }
    uint8_t pulse[] = {port, port | SCLCD_ENABLE_MASK, port};
    // PCF8574는 레지스터 주소 없이 연속된 각 바이트를 출력 포트에 기록.
    // 100kHz에서 바이트 간격이 LCD의 EN 펄스/데이터 setup 시간을 충족.
    if (HAL_I2C_Master_Transmit(lcd.hi2c, lcd.address, pulse, sizeof(pulse), SCLCD_I2C_TIMEOUT_MS) != HAL_OK)
    {
        lcd.initialized = false;
        return false;
    }
    lcd.portValue = port;
    return true;
}

static bool WriteByte(uint8_t value, bool dataMode)
{
    if (!WriteNibble(value, dataMode) || !WriteNibble((uint8_t)(value << 4), dataMode))
    {
        return false;
    }
    // RW로 busy flag를 읽지 않으므로 일반 명령/문자의 실행 시간을 기다림.
    HAL_Delay(1);
    return true;
}

static bool SendCommand(uint8_t command)
{
    if (!WriteByte(command, false))
    {
        return false;
    }
    if (command == SCLCD_CLEAR_COMMAND || command == SCLCD_HOME_COMMAND)
    {
        HAL_Delay(2);
    }
    return true;
}

bool SClcdInit(I2C_HandleTypeDef *hi2c, uint8_t address)
{
    memset(&lcd, 0, sizeof(lcd));
    if (hi2c == NULL ||
        !((address >= 0x20U && address <= 0x27U) || (address >= 0x38U && address <= 0x3FU)))
    {
        return false;
    }
    lcd.hi2c = hi2c;
    // STM32 HALは7ビットアドレスを1ビット左シフトした値を要求。
    lcd.address = (uint16_t)address << 1;
    lcd.backlightMask = SCLCD_BACKLIGHT_MASK;

    HAL_Delay(SCLCD_POWER_ON_MS);
    if (HAL_I2C_IsDeviceReady(hi2c, lcd.address, 2, SCLCD_I2C_TIMEOUT_MS) != HAL_OK ||
        !WritePort(lcd.backlightMask))
    {
        return false;
    }

    // 전원 투입 시의 8비트/4비트 상태에 관계없이 동기화하고 4비트로 전환.
    // 처음 3회는 상위 니블 0x3만 전송. 전체 바이트 0x33 등으로 보내지 않음.
    if (!WriteNibble(0x30U, false))
    {
        return false;
    }
    HAL_Delay(5);
    if (!WriteNibble(0x30U, false))
    {
        return false;
    }
    HAL_Delay(1);
    if (!WriteNibble(0x30U, false))
    {
        return false;
    }
    HAL_Delay(1);
    if (!WriteNibble(0x20U, false))
    {
        return false;
    }
    HAL_Delay(1);

    if (!SendCommand(SCLCD_FUNCTION_COMMAND) ||
        !SendCommand(SCLCD_DISPLAY_COMMAND) ||
        !SendCommand(SCLCD_CLEAR_COMMAND) ||
        !SendCommand(SCLCD_ENTRY_MODE_COMMAND) ||
        !SendCommand(SCLCD_DISPLAY_COMMAND | SCLCD_DISPLAY_ON_MASK))
    {
        return false;
    }
    lcd.initialized = true;
    return true;
}

bool SClcdClear(void)
{
    return lcd.initialized && SendCommand(SCLCD_CLEAR_COMMAND);
}

bool SClcdHome(void)
{
    return lcd.initialized && SendCommand(SCLCD_HOME_COMMAND);
}

bool SClcdSetCursor(uint8_t column, uint8_t row)
{
    if (!lcd.initialized || column >= SCLCD_COLUMNS || row >= SCLCD_ROWS)
    {
        return false;
    }
    uint8_t address = column + (row == 0 ? 0 : SCLCD_SECOND_ROW_ADDRESS);
    return SendCommand(SCLCD_DDRAM_COMMAND | address);
}

bool SClcdWriteChar(char character)
{
    return lcd.initialized && WriteByte((uint8_t)character, true);
}

bool SClcdWriteString(const char *text)
{
    if (!lcd.initialized || text == NULL)
    {
        return false;
    }
    while (*text != '\0')
    {
        if (!SClcdWriteChar(*text++))
        {
            return false;
        }
    }
    return true;
}

bool SClcdPrintf(const char *format, ...)
{
    if (!lcd.initialized || format == NULL)
    {
        return false;
    }
    char text[SCLCD_PRINT_SIZE];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (length < 0 || length >= (int)sizeof(text))
    {
        return false;
    }
    return SClcdWriteString(text);
}

bool SClcdSetBacklight(bool enabled)
{
    if (!lcd.initialized)
    {
        return false;
    }
    uint8_t mask = enabled ? SCLCD_BACKLIGHT_MASK : 0;
    uint8_t port = (lcd.portValue & (uint8_t)~SCLCD_BACKLIGHT_MASK) | mask;
    if (!WritePort(port))
    {
        return false;
    }
    lcd.backlightMask = mask;
    return true;
}

bool SClcdSetDisplay(bool enabled)
{
    uint8_t command = SCLCD_DISPLAY_COMMAND | (enabled ? SCLCD_DISPLAY_ON_MASK : 0);
    return lcd.initialized && SendCommand(command);
}
