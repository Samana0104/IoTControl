#include "IoTFirmware.h"

/* Shared by Raspberry5, the STM32 application and bootloader: no dynamic allocation, no stdio. */

static uint32_t ReadUint32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint32_t ComputeFirmwareCrc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for(size_t index = 0; index < length; ++index)
    {
        crc ^= data[index];
        for(int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1U) != 0 ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

int CheckFirmwareImage(const uint8_t *image, size_t size, uint32_t *version)
{
    uint32_t stackPointer;
    uint32_t resetVector;
    uint32_t resetAddress;

    if(image == NULL || version == NULL || size < FIRMWARE_INFO_OFFSET + sizeof(FirmwareInfo) || size > FIRMWARE_APP_MAX_SIZE)
    {
        return -1;
    }
    stackPointer = ReadUint32(image);
    resetVector = ReadUint32(image + 4);
    resetAddress = resetVector & ~1UL;
    /* Stack pointer inside SRAM (end included); reset vector is a Thumb address inside the image. */
    if(stackPointer <= FIRMWARE_RAM_START || stackPointer > FIRMWARE_RAM_END || (stackPointer & 3U) != 0)
    {
        return -1;
    }
    if((resetVector & 1U) == 0 || resetAddress < FIRMWARE_APP_ADDRESS || resetAddress >= FIRMWARE_APP_ADDRESS + size)
    {
        return -1;
    }
    if(ReadUint32(image + FIRMWARE_INFO_OFFSET) != FIRMWARE_INFO_MAGIC)
    {
        return -1;
    }
    *version = ReadUint32(image + FIRMWARE_INFO_OFFSET + 4);
    return 0;
}
