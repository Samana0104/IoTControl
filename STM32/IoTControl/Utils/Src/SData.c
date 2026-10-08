#include "SData.h"
#include "SLog.h"

#define SDATA_MAGIC 0xA5
#define SDATA_ERASED_BYTE 0xFF

// STM32F411RE 마지막 섹터 (링커 스크립트 FLASH LENGTH와 맞춰야 함)
#define SDATA_SECTOR FLASH_SECTOR_7
#define SDATA_SECTOR_ADDR 0x08060000UL
#define SDATA_SECTOR_SIZE (128UL * 1024UL)

// 블록 = [header 4바이트][image SDATA_SIZE바이트]
//   header는 image를 다 쓴 뒤 마지막에 씀 → header가 있으면 완전히 써진 블록
#define SDATA_BLOCK_HEADER 0x53444154UL // "SDAT"
#define SDATA_BLOCK_SIZE (sizeof(uint32_t) + SDATA_SIZE)
#define SDATA_BLOCK_COUNT (SDATA_SECTOR_SIZE / SDATA_BLOCK_SIZE)

static uint32_t imageWords[SDATA_SIZE / sizeof(uint32_t)];
static uint8_t *const image = (uint8_t *)imageWords;

static bool initialized = false;
// 다음 저장을 시작할 블록 (마지막 저장 블록 + 1)
static uint32_t nextBlock = 0;

static uint32_t BlockAddr(uint32_t block)
{
    return SDATA_SECTOR_ADDR + block * SDATA_BLOCK_SIZE;
}

static bool IsBlockErased(uint32_t block)
{
    const uint32_t *p = (const uint32_t *)BlockAddr(block);
    for (uint32_t i = 0; i < SDATA_BLOCK_SIZE / sizeof(uint32_t); ++i)
    {
        if (p[i] != 0xFFFFFFFFUL)
        {
            return false;
        }
    }
    return true;
}

// 섹터에서 header가 있는 마지막 블록을 RAM image로 읽음, 없으면 빈 image
static void LoadImage(void)
{
    memset(image, SDATA_ERASED_BYTE, SDATA_SIZE);
    nextBlock = 0;

    for (uint32_t block = 0; block < SDATA_BLOCK_COUNT; ++block)
    {
        if (*(const uint32_t *)BlockAddr(block) == SDATA_BLOCK_HEADER)
        {
            memcpy(image, (const uint8_t *)(BlockAddr(block) + sizeof(uint32_t)), SDATA_SIZE);
            nextBlock = block + 1;
        }
    }
}

static bool EraseSector(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = SDATA_SECTOR;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t sectorError = 0;
    return HAL_FLASHEx_Erase(&erase, &sectorError) == HAL_OK;
}

static bool ProgramBlock(uint32_t block)
{
    uint32_t addr = BlockAddr(block);

    for (uint32_t i = 0; i < SDATA_SIZE / sizeof(uint32_t); ++i)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + sizeof(uint32_t) + i * sizeof(uint32_t),
                              imageWords[i]) != HAL_OK)
        {
            return false;
        }
    }
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, SDATA_BLOCK_HEADER) == HAL_OK;
}

// RAM image를 다음 빈 블록에 씀, 빈 블록이 없으면 섹터를 지우고 0번 블록에 씀
static bool CommitImage(void)
{
    // 쓰다가 전원이 꺼진 블록(header 없이 일부만 써짐)은 건너뜀
    uint32_t block = nextBlock;
    while (block < SDATA_BLOCK_COUNT && !IsBlockErased(block))
    {
        ++block;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    bool ok = true;
    if (block >= SDATA_BLOCK_COUNT)
    {
        SLOG_INFO("data sector full, erasing");
        block = 0;
        ok = EraseSector();
    }
    ok = ok && ProgramBlock(block);

    HAL_FLASH_Lock();

    // 실제로 써진 값 확인
    ok = ok && memcmp((const uint8_t *)(BlockAddr(block) + sizeof(uint32_t)), image, SDATA_SIZE) == 0;
    if (!ok)
    {
        SLOG_ERROR("data flash write failed: block=%lu, error=0x%08lX", (unsigned long)block,
                   (unsigned long)HAL_FLASH_GetError());
        // RAM image를 플래시에 남은 마지막 값으로 되돌림
        LoadImage();
        return false;
    }

    nextBlock = block + 1;
    return true;
}

// magic 1바이트 + data가 image 안에 들어가는지
static bool IsInRange(uint16_t addr, uint16_t length)
{
    return (uint32_t)addr + 1 + length <= SDATA_SIZE;
}

bool SDataInit(void)
{
    LoadImage();
    initialized = true;
    SLOG_INFO("data loaded: next block=%lu/%lu", (unsigned long)nextBlock, (unsigned long)SDATA_BLOCK_COUNT);
    return true;
}

bool SDataSave(uint16_t addr, const uint8_t *data, uint16_t length)
{
    if (!initialized || data == NULL || length == 0 || !IsInRange(addr, length))
    {
        return false;
    }

    // 값이 같으면 플래시에 다시 쓰지 않음 (플래시 수명 보호)
    if (image[addr] == SDATA_MAGIC && memcmp(&image[addr + 1], data, length) == 0)
    {
        return true;
    }

    image[addr] = SDATA_MAGIC;
    memcpy(&image[addr + 1], data, length);
    return CommitImage();
}

bool SDataLoad(uint16_t addr, uint8_t *data, uint16_t length)
{
    if (!initialized || data == NULL || length == 0 || !IsInRange(addr, length))
    {
        return false;
    }

    if (image[addr] != SDATA_MAGIC)
    {
        return false;
    }

    memcpy(data, &image[addr + 1], length);
    return true;
}

bool SDataClear(uint16_t addr, uint16_t length)
{
    if (!initialized || !IsInRange(addr, length))
    {
        return false;
    }

    // 이미 지워져 있으면 플래시에 다시 쓰지 않음
    bool erased = true;
    for (uint16_t i = 0; i <= length; ++i)
    {
        if (image[addr + i] != SDATA_ERASED_BYTE)
        {
            erased = false;
            break;
        }
    }
    if (erased)
    {
        return true;
    }

    memset(&image[addr], SDATA_ERASED_BYTE, (size_t)length + 1);
    return CommitImage();
}
