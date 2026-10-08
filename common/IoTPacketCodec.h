#pragma once

#include "IoTPacket.h"
#include "IoTPacketStream.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A frame is the HEADER_SIZE header + payload, sent at once.
   Buffers of HEADER_SIZE + <payload size> (or PACKET_FRAME_SIZE) always fit. */

/* Payload length rule of each cmd in IoTProtocol.h.
   0: known cmd with a valid length, -1: unknown cmd or wrong length. */
int CheckPacketLength(uint16_t cmd, size_t length);

/* ---- Make*Packet: one complete frame. Returns the frame length, or 0 if size is too small. ---- */
/* Current authenticated field-device sessions, not database rows. */
size_t MakeSessionAllPacket(uint8_t *buffer, size_t size);
size_t MakeSessionRowPacket(uint8_t *buffer, size_t size, const SessionRowData *data);
size_t MakeDhtPacket(uint8_t *buffer, size_t size, const DhtData *data);
/* Server SELECT: zero or more NFY_DHT_ROW frames, then ACK_DHT_ALL. */
size_t MakeDhtAllPacket(uint8_t *buffer, size_t size);
size_t MakeDhtRowPacket(uint8_t *buffer, size_t size, const DhtRowData *data);
size_t MakeFanPacket(uint8_t *buffer, size_t size, const FanData *data);
/* Read the server DB fan value; this never changes the device speed. */
size_t MakeFanQueryPacket(uint8_t *buffer, size_t size);
size_t MakeFanApplyPacket(uint8_t *buffer, size_t size, const FanApplyData *data);
size_t MakeFanApplyAckPacket(uint8_t *buffer, size_t size, const FanApplyAckData *data);
/* UPDATE the existing server DB fan row; never sends REQ_FAN to a device. */
size_t MakeFanUpdatePacket(uint8_t *buffer, size_t size, const FanData *data);
size_t MakeFanQueryAckPacket(uint8_t *buffer, size_t size, const FanQueryAckData *data);
/* REQ_FAN: server -> device, fanSpeed is a 0..100 percent. */
size_t MakeFanControlPacket(uint8_t *buffer, size_t size, const FanData *data);
size_t MakeConPacket(uint8_t *buffer, size_t size, const ConData *data);
size_t MakeLoginPacket(uint8_t *buffer, size_t size, const MemData *data);
size_t MakeChatPacket(uint8_t *buffer, size_t size, const char *message, size_t length);
size_t MakeBluetoothRegisterPacket(uint8_t *buffer, size_t size, const BluetoothRegisterData *data);
size_t MakeBluetoothConnectPacket(uint8_t *buffer, size_t size, const BluetoothConnectData *data);
/* REQ_DHT: server -> device, no payload. */
size_t MakeDhtRequestPacket(uint8_t *buffer, size_t size);
/* REQ_DHT_COLLECT: PC -> server, no payload. */
size_t MakeDhtCollectPacket(uint8_t *buffer, size_t size);
/* ACK_DHT: device -> server, reply to REQ_DHT. */
size_t MakeDhtAckPacket(uint8_t *buffer, size_t size, const DhtAckData *data);
/* REQ_FW_*: server -> STM32. REQ_FW_END has no payload. */
size_t MakeFirmwareBeginPacket(uint8_t *buffer, size_t size, const FirmwareBeginData *data);
size_t MakeFirmwareChunkPacket(uint8_t *buffer, size_t size, const FirmwareChunkData *data);
size_t MakeFirmwareEndPacket(uint8_t *buffer, size_t size);
/* ACK_FW_CHUNK: STM32 -> server. ACK_FW_BEGIN/ACK_FW_END use MakeAckPacket. */
size_t MakeFirmwareChunkAckPacket(uint8_t *buffer, size_t size, const FirmwareChunkAckData *data);
/* ACK for reqCmd (a REQ_* value) with one RESULT_* byte. */
size_t MakeAckPacket(uint8_t *buffer, size_t size, uint16_t reqCmd, uint8_t result);

/* ---- Read*: payload (without header) -> struct. Returns 0 only for the exact payload size. ---- */
int ReadSessionRowData(const uint8_t *payload, size_t length, SessionRowData *data);
int ReadDhtData(const uint8_t *payload, size_t length, DhtData *data);
int ReadDhtRowData(const uint8_t *payload, size_t length, DhtRowData *data);
int ReadFanData(const uint8_t *payload, size_t length, FanData *data);
int ReadFanApplyData(const uint8_t *payload, size_t length, FanApplyData *data);
int ReadFanApplyAckData(const uint8_t *payload, size_t length, FanApplyAckData *data);
int ReadFanQueryAckData(const uint8_t *payload, size_t length, FanQueryAckData *data);
int ReadConData(const uint8_t *payload, size_t length, ConData *data);
int ReadMemData(const uint8_t *payload, size_t length, MemData *data);
int ReadBluetoothRegisterData(const uint8_t *payload, size_t length, BluetoothRegisterData *data);
int ReadBluetoothConnectData(const uint8_t *payload, size_t length, BluetoothConnectData *data);
int ReadDhtAckData(const uint8_t *payload, size_t length, DhtAckData *data);
int ReadFirmwareBeginData(const uint8_t *payload, size_t length, FirmwareBeginData *data);
/* data->length = payload length - FIRMWARE_CHUNK_OFFSET_SIZE (1..FIRMWARE_CHUNK_SIZE) */
int ReadFirmwareChunkData(const uint8_t *payload, size_t length, FirmwareChunkData *data);
int ReadFirmwareChunkAckData(const uint8_t *payload, size_t length, FirmwareChunkAckData *data);
int ReadResultData(const uint8_t *payload, size_t length, ResultData *data);

#ifdef __cplusplus
}
#endif
