#include "AData.h"

#include <EEPROM.h>

static constexpr uint8_t DATA_MAGIC = 0xA5;
static constexpr uint8_t ERASED_BYTE = 0xFF;

// magic 1바이트 + data가 EEPROM 안에 들어가는지
static bool IsInRange(uint16_t addr, size_t length)
{
    return (size_t)addr + 1 + length <= EEPROM.length();
}

bool SaveData(uint16_t addr, const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0 || !IsInRange(addr, length))
    {
        return false;
    }

    // update()는 값이 바뀐 바이트만 다시 씀 (EEPROM 수명 보호)
    for (size_t i = 0; i < length; ++i)
    {
        EEPROM.update(addr + 1 + i, data[i]);
    }
    EEPROM.update(addr, DATA_MAGIC);
    return true;
}

bool LoadData(uint16_t addr, uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0 || !IsInRange(addr, length))
    {
        return false;
    }

    if (EEPROM.read(addr) != DATA_MAGIC)
    {
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        data[i] = EEPROM.read(addr + 1 + i);
    }
    return true;
}

void ClearData(uint16_t addr, size_t length)
{
    if (!IsInRange(addr, length))
    {
        return;
    }

    for (size_t i = 0; i <= length; ++i)
    {
        EEPROM.update(addr + i, ERASED_BYTE);
    }
}
