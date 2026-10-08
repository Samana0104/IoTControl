#include "SPacketFirmware.h"
#include "SIotProtocol.h"
#include "SLog.h"
#include "IoTPacketCodec.h"

#include <stddef.h>

// ACK_FW_END를 보낸 뒤 재부팅까지 기다리는 시간 (UART → HC-05 → 서버까지 나갈 시간)
#define FIRMWARE_REBOOT_DELAY_MS 500U
// 청크 로그 간격 (매 청크마다 찍으면 수신이 느려짐)
#define FIRMWARE_LOG_INTERVAL_BYTES (16U * 1024U)

typedef struct _SFirmwareReceive
{
    bool receiving;          // REQ_FW_BEGIN 성공 후 REQ_FW_END 전까지
    FirmwareBeginData begin; // 받을 이미지 크기, CRC32, 버전
    uint32_t writtenSize;    // 오프셋 0부터 연속으로 쓴 바이트 수
    bool rebootPending;
    uint32_t rebootTick;
} SFirmwareReceive;

static SFirmwareReceive receive;

// 링커 스크립트가 .firmware_info를 앱 시작 + FIRMWARE_INFO_OFFSET에 둠 (부트로더·서버가 이 값으로 버전 확인)
__attribute__((section(".firmware_info"), used)) const FirmwareInfo firmwareInfo = {FIRMWARE_INFO_MAGIC, FIRMWARE_VERSION};

static bool EraseStaging(void);
static bool ProgramStaging(uint32_t address, const uint8_t *data, uint16_t length);
static bool WriteStagingHeader(const FirmwareBeginData *begin);
static void SendResultAck(uint16_t ackCmd, uint8_t result);

void SPacketFirmwareBeginReceive(const uint8_t *payload, uint16_t length)
{
    FirmwareBeginData begin;
    uint8_t result = RESULT_FAIL;

    receive.receiving = false;
    if (ReadFirmwareBeginData(payload, length, &begin) != 0 || begin.size == 0 || begin.size > FIRMWARE_APP_MAX_SIZE)
    {
        SLOG_WARN("fw begin rejected: length=%u", (unsigned int)length);
    }
    else if (!EraseStaging())
    {
        SLOG_ERROR("fw staging erase failed: error=0x%08lX", (unsigned long)HAL_FLASH_GetError());
    }
    else
    {
        receive.begin = begin;
        receive.writtenSize = 0;
        receive.receiving = true;
        result = RESULT_SUCCESS;
        SLOG_INFO("fw begin: version=%lu, size=%lu (running version=%lu)", (unsigned long)begin.version,
                  (unsigned long)begin.size, (unsigned long)firmwareInfo.version);
    }
    SendResultAck(ACK_FW_BEGIN, result);
}

void SPacketFirmwareChunkReceive(const uint8_t *payload, uint16_t length)
{
    FirmwareChunkData chunk;
    FirmwareChunkAckData ack = {RESULT_FAIL, 0};
    uint8_t frame[HEADER_SIZE + FIRMWARE_CHUNK_ACK_DATA_SIZE];
    size_t frameLength;

    if (ReadFirmwareChunkData(payload, length, &chunk) != 0)
    {
        SLOG_WARN("fw chunk malformed: length=%u", (unsigned int)length);
    }
    else
    {
        ack.offset = chunk.offset;
        if (!receive.receiving || chunk.offset > receive.writtenSize || chunk.length > receive.begin.size - chunk.offset)
        {
            SLOG_WARN("fw chunk rejected: offset=%lu, written=%lu", (unsigned long)chunk.offset,
                      (unsigned long)receive.writtenSize);
        }
        else if (chunk.offset + chunk.length <= receive.writtenSize)
        {
            // ACK가 유실돼 서버가 다시 보낸 청크: 이미 썼으므로 그대로 성공
            ack.result = RESULT_SUCCESS;
        }
        else if (chunk.offset != receive.writtenSize)
        {
            SLOG_WARN("fw chunk overlaps: offset=%lu, written=%lu", (unsigned long)chunk.offset,
                      (unsigned long)receive.writtenSize);
        }
        else if (!ProgramStaging(FIRMWARE_STAGING_IMAGE_ADDRESS + chunk.offset, chunk.data, chunk.length))
        {
            SLOG_ERROR("fw chunk write failed: offset=%lu, error=0x%08lX", (unsigned long)chunk.offset,
                       (unsigned long)HAL_FLASH_GetError());
        }
        else
        {
            receive.writtenSize += chunk.length;
            ack.result = RESULT_SUCCESS;
            if (receive.writtenSize % FIRMWARE_LOG_INTERVAL_BYTES < chunk.length || receive.writtenSize == receive.begin.size)
            {
                SLOG_INFO("fw received %lu/%lu", (unsigned long)receive.writtenSize, (unsigned long)receive.begin.size);
            }
        }
    }

    // SIotProtocolSendPacket은 payload만 받으므로 공용 코덱으로 프레임을 만든 뒤 payload 부분을 넘김
    frameLength = MakeFirmwareChunkAckPacket(frame, sizeof(frame), &ack);
    if (frameLength == 0)
    {
        SLOG_ERROR("ACK_FW_CHUNK build failed");
        return;
    }
    SIotProtocolSendPacket(ACK_FW_CHUNK, frame + HEADER_SIZE, (uint16_t)(frameLength - HEADER_SIZE));
}

void SPacketFirmwareEndReceive(const uint8_t *payload, uint16_t length)
{
    const uint8_t *image = (const uint8_t *)FIRMWARE_STAGING_IMAGE_ADDRESS;
    uint32_t imageVersion = 0;
    uint8_t result = RESULT_FAIL;

    (void)payload;
    (void)length;
    if (!receive.receiving || receive.writtenSize != receive.begin.size)
    {
        SLOG_WARN("fw end rejected: written=%lu, size=%lu", (unsigned long)receive.writtenSize,
                  (unsigned long)receive.begin.size);
    }
    else
    {
        // 방금 쓴 플래시를 데이터 캐시의 옛 값이 아니라 실제 값으로 읽도록 캐시를 비움
        __HAL_FLASH_DATA_CACHE_DISABLE();
        __HAL_FLASH_DATA_CACHE_RESET();
        __HAL_FLASH_DATA_CACHE_ENABLE();
        if (ComputeFirmwareCrc32(image, receive.begin.size) != receive.begin.crc32)
        {
            SLOG_ERROR("fw end: CRC32 mismatch");
        }
        else if (CheckFirmwareImage(image, receive.begin.size, &imageVersion) != 0 || imageVersion != receive.begin.version)
        {
            SLOG_ERROR("fw end: not an application image for this board");
        }
        else if (!WriteStagingHeader(&receive.begin))
        {
            SLOG_ERROR("fw end: staging header write failed: error=0x%08lX", (unsigned long)HAL_FLASH_GetError());
        }
        else
        {
            result = RESULT_SUCCESS;
            receive.rebootPending = true;
            receive.rebootTick = HAL_GetTick();
            SLOG_INFO("fw ready: version=%lu, rebooting to install", (unsigned long)imageVersion);
        }
    }
    receive.receiving = false;
    SendResultAck(ACK_FW_END, result);
}

void SPacketFirmwareUpdate(void)
{
    if (receive.rebootPending && HAL_GetTick() - receive.rebootTick >= FIRMWARE_REBOOT_DELAY_MS)
    {
        NVIC_SystemReset();
    }
}

static bool EraseStaging(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sectorError = 0;
    bool ok;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FIRMWARE_STAGING_SECTOR;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    ok = HAL_FLASHEx_Erase(&erase, &sectorError) == HAL_OK;
    HAL_FLASH_Lock();
    return ok;
}

// 청크 길이와 오프셋이 4의 배수가 아닐 수 있으므로 바이트 단위로 씀
static bool ProgramStaging(uint32_t address, const uint8_t *data, uint16_t length)
{
    bool ok = true;

    HAL_FLASH_Unlock();
    for (uint16_t index = 0; ok && index < length; ++index)
    {
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address + index, data[index]) == HAL_OK;
    }
    HAL_FLASH_Lock();
    return ok;
}

// magic을 마지막에 써야 중간에 끊겨도 부트로더가 덜 받은 이미지를 설치하지 않음
static bool WriteStagingHeader(const FirmwareBeginData *begin)
{
    bool ok;

    HAL_FLASH_Unlock();
    ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FIRMWARE_STAGING_ADDRESS + offsetof(FirmwareStagingHeader, version), begin->version) == HAL_OK &&
         HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FIRMWARE_STAGING_ADDRESS + offsetof(FirmwareStagingHeader, size), begin->size) == HAL_OK &&
         HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FIRMWARE_STAGING_ADDRESS + offsetof(FirmwareStagingHeader, crc32), begin->crc32) == HAL_OK &&
         HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FIRMWARE_STAGING_ADDRESS + offsetof(FirmwareStagingHeader, magic), FIRMWARE_STAGING_MAGIC) == HAL_OK;
    HAL_FLASH_Lock();
    return ok;
}

static void SendResultAck(uint16_t ackCmd, uint8_t result)
{
    if (!SIotProtocolSendPacket(ackCmd, &result, RESULT_DATA_SIZE))
    {
        SLOG_ERROR("fw ack send failed: cmd=0x%04X", (unsigned int)ackCmd);
    }
}
