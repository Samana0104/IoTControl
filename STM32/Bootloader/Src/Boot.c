#include "stm32f4xx.h"
#include "IoTFirmware.h"

#include <stdbool.h>
#include <stddef.h>

// 섹터 0에 있는 부트로더. 리셋 직후(HSI 16MHz, 인터럽트 없음) 실행됨.
//   1. 대기 영역(섹터 5)에 완성된 이미지가 있고 앱 영역과 내용이 다르면 앱 영역(섹터 1~4)으로 복사
//   2. 복사·검증이 끝나면 대기 헤더의 magic을 지움 (중간에 전원이 꺼지면 다음 부팅 때 다시 복사)
//   3. 앱 영역에 올바른 이미지가 있으면 앱으로 점프, 없으면 멈춤 (ST-Link로 앱을 구워야 함)
// HAL 없이 레지스터로 플래시를 다룸 (16KB 안에 들어가도록)

#define BOOT_FLASH_KEY1 0x45670123UL
#define BOOT_FLASH_KEY2 0xCDEF89ABUL
#define BOOT_FLASH_ERRORS (FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)
// 한 번 부팅할 때 복사를 다시 시도하는 횟수
#define BOOT_INSTALL_TRIES 3

static bool IsStagingReady(const FirmwareStagingHeader *staging);
static bool IsApplicationSame(const FirmwareStagingHeader *staging);
static bool InstallStaging(const FirmwareStagingHeader *staging);
static void ClearStaging(void);
static bool IsApplicationValid(void);
static void JumpToApplication(void) __attribute__((noreturn));
static void UnlockFlash(void);
static void LockFlash(void);
static bool WaitFlash(void);
static bool EraseSector(uint32_t sector);
static bool ProgramWord(uint32_t address, uint32_t value);

int main(void)
{
    const FirmwareStagingHeader *staging = (const FirmwareStagingHeader *)FIRMWARE_STAGING_ADDRESS;

    if(IsStagingReady(staging))
    {
        for(int tryCount = 0; tryCount < BOOT_INSTALL_TRIES && !IsApplicationSame(staging); ++tryCount)
        {
            InstallStaging(staging);
        }
        // 같은 이미지를 다시 받은 경우도 여기서 정리됨. 끝내 실패하면 magic을 남겨 다음 부팅 때 다시 시도
        if(IsApplicationSame(staging))
        {
            ClearStaging();
        }
    }
    if(IsApplicationValid())
    {
        JumpToApplication();
    }
    while(1)
    {
    }
}

// magic은 앱이 CRC32를 확인한 뒤 마지막에 씀. 그래도 플래시 내용을 다시 확인함
static bool IsStagingReady(const FirmwareStagingHeader *staging)
{
    const uint8_t *image = (const uint8_t *)FIRMWARE_STAGING_IMAGE_ADDRESS;
    uint32_t imageVersion;

    if(staging->magic != FIRMWARE_STAGING_MAGIC || staging->size == 0 || staging->size > FIRMWARE_APP_MAX_SIZE)
    {
        return false;
    }
    if(ComputeFirmwareCrc32(image, staging->size) != staging->crc32)
    {
        return false;
    }
    return CheckFirmwareImage(image, staging->size, &imageVersion) == 0 && imageVersion == staging->version;
}

// 버전이 아니라 내용(CRC32)으로 비교: 복사가 중간에 끊겨 앞부분만 새 버전인 경우도 다르다고 판단함
static bool IsApplicationSame(const FirmwareStagingHeader *staging)
{
    return ComputeFirmwareCrc32((const uint8_t *)FIRMWARE_APP_ADDRESS, staging->size) == staging->crc32;
}

static bool InstallStaging(const FirmwareStagingHeader *staging)
{
    const uint32_t *source = (const uint32_t *)FIRMWARE_STAGING_IMAGE_ADDRESS;
    uint32_t wordCount = (staging->size + 3U) / 4U;
    bool ok = true;

    UnlockFlash();
    for(uint32_t sector = FIRMWARE_APP_FIRST_SECTOR; ok && sector <= FIRMWARE_APP_LAST_SECTOR; ++sector)
    {
        ok = EraseSector(sector);
    }
    // 마지막 워드의 남는 바이트는 대기 영역의 지워진 값(0xFF)이 그대로 들어감
    for(uint32_t index = 0; ok && index < wordCount; ++index)
    {
        ok = ProgramWord(FIRMWARE_APP_ADDRESS + index * 4U, source[index]);
    }
    LockFlash();
    return ok;
}

// 지우지 않고 magic 워드만 0으로 씀 (플래시는 1→0 쓰기가 가능)
static void ClearStaging(void)
{
    UnlockFlash();
    ProgramWord(FIRMWARE_STAGING_ADDRESS + offsetof(FirmwareStagingHeader, magic), 0);
    LockFlash();
}

static bool IsApplicationValid(void)
{
    uint32_t version;

    return CheckFirmwareImage((const uint8_t *)FIRMWARE_APP_ADDRESS, FIRMWARE_APP_MAX_SIZE, &version) == 0;
}

// 스택 포인터를 바꾼 뒤에는 스택 변수를 읽을 수 없으므로 MSP 설정과 점프를 한 번에 함
static void JumpToApplication(void)
{
    const volatile uint32_t *vectors = (const volatile uint32_t *)FIRMWARE_APP_ADDRESS;
    uint32_t stackPointer = vectors[0];
    uint32_t resetHandler = vectors[1];

    SCB->VTOR = FIRMWARE_APP_ADDRESS;
    __DSB();
    __ISB();
    __asm volatile("msr msp, %0\n\tbx %1" : : "r"(stackPointer), "r"(resetHandler) : "memory");
    __builtin_unreachable();
}

static void UnlockFlash(void)
{
    if((FLASH->CR & FLASH_CR_LOCK) != 0)
    {
        FLASH->KEYR = BOOT_FLASH_KEY1;
        FLASH->KEYR = BOOT_FLASH_KEY2;
    }
    FLASH->SR = BOOT_FLASH_ERRORS;
}

static void LockFlash(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static bool WaitFlash(void)
{
    while((FLASH->SR & FLASH_SR_BSY) != 0)
    {
    }
    if((FLASH->SR & BOOT_FLASH_ERRORS) != 0)
    {
        FLASH->SR = BOOT_FLASH_ERRORS;
        return false;
    }
    return true;
}

// 32비트 병렬 쓰기 (PSIZE x32, 전원 2.7~3.6V)
static bool EraseSector(uint32_t sector)
{
    bool ok;

    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    ok = WaitFlash();
    FLASH->CR = 0;
    return ok;
}

static bool ProgramWord(uint32_t address, uint32_t value)
{
    bool ok;

    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    *(volatile uint32_t *)address = value;
    ok = WaitFlash();
    FLASH->CR = 0;
    return ok;
}
