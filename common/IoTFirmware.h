#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   STM32F411 (512KB) firmware update layout, shared by the server, the STM32
   application and the STM32 bootloader. Sectors: 0-3 16KB, 4 64KB, 5-7 128KB.

   sector 0    0x08000000  16KB   bootloader
   sector 1-4  0x08004000  112KB  application (vector table, FirmwareInfo at +0x200)
   sector 5    0x08020000  128KB  staging: FirmwareStagingHeader at +0, image at +0x100
   sector 6    0x08040000  128KB  unused
   sector 7    0x08060000  128KB  SData (settings), never touched by updates

   The application receives an image into staging (REQ_FW_*) and writes the
   staging header last, only after the CRC32 matches. On boot the bootloader
   copies a ready staging image into the application area when they differ,
   verifies it, then clears the staging magic. A copy cut by a power loss is
   retried on the next boot because the staging magic is still set.
   ============================================================================ */
#define FIRMWARE_APP_ADDRESS 0x08004000UL
#define FIRMWARE_APP_MAX_SIZE (112UL * 1024UL)
#define FIRMWARE_APP_FIRST_SECTOR 1U
#define FIRMWARE_APP_LAST_SECTOR 4U

#define FIRMWARE_STAGING_ADDRESS 0x08020000UL
#define FIRMWARE_STAGING_SECTOR 5U
#define FIRMWARE_STAGING_IMAGE_OFFSET 0x100UL
#define FIRMWARE_STAGING_IMAGE_ADDRESS (FIRMWARE_STAGING_ADDRESS + FIRMWARE_STAGING_IMAGE_OFFSET)
#define FIRMWARE_STAGING_MAGIC 0x47545346UL /* "FSTG" */

/* FirmwareInfo position inside an application image (right after the vector table). */
#define FIRMWARE_INFO_OFFSET 0x200UL
#define FIRMWARE_INFO_MAGIC 0x46544F49UL /* "IOTF" */

/* Initial stack pointer must point into SRAM. */
#define FIRMWARE_RAM_START 0x20000000UL
#define FIRMWARE_RAM_END 0x20020000UL

/* Placed by the application linker script at FIRMWARE_APP_ADDRESS + FIRMWARE_INFO_OFFSET. */
typedef struct _FirmwareInfo
{
    uint32_t magic; /* FIRMWARE_INFO_MAGIC */
    uint32_t version;
} FirmwareInfo;

/* First 16 bytes of the staging sector. magic is programmed last. */
typedef struct _FirmwareStagingHeader
{
    uint32_t magic; /* FIRMWARE_STAGING_MAGIC: image is complete and verified */
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
} FirmwareStagingHeader;

/* CRC-32 (IEEE 802.3, reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF), same as zlib crc32(). */
uint32_t ComputeFirmwareCrc32(const uint8_t *data, size_t length);

/* Checks that image is an application linked for FIRMWARE_APP_ADDRESS:
   size, initial stack pointer, reset vector and FirmwareInfo magic.
   0: valid (*version set), -1: not a valid image. */
int CheckFirmwareImage(const uint8_t *image, size_t size, uint32_t *version);

#ifdef __cplusplus
}
#endif
