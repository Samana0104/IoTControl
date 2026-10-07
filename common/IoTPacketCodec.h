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

/* ---- Make*Packet: one complete frame. Returns the frame length, or 0 if size is too small. ---- */
size_t MakeDhtPacket(uint8_t *buffer, size_t size, const DhtData *data);
size_t MakeFanPacket(uint8_t *buffer, size_t size, const FanData *data);
/* REQ_FAN: server -> device, fanSpeed is a 0..100 percent. */
size_t MakeFanControlPacket(uint8_t *buffer, size_t size, const FanData *data);
size_t MakeConPacket(uint8_t *buffer, size_t size, const ConData *data);
size_t MakeLoginPacket(uint8_t *buffer, size_t size, const MemData *data);
size_t MakeChatPacket(uint8_t *buffer, size_t size, const char *message, size_t length);
size_t MakeBluetoothRegisterPacket(uint8_t *buffer, size_t size, const BluetoothRegisterData *data);
size_t MakeBluetoothConnectPacket(uint8_t *buffer, size_t size, const BluetoothConnectData *data);
/* ACK for reqCmd (a REQ_* value) with one RESULT_* byte. */
size_t MakeAckPacket(uint8_t *buffer, size_t size, uint16_t reqCmd, uint8_t result);

/* ---- Read*: payload (without header) -> struct. Returns 0 only for the exact payload size. ---- */
int ReadDhtData(const uint8_t *payload, size_t length, DhtData *data);
int ReadFanData(const uint8_t *payload, size_t length, FanData *data);
int ReadConData(const uint8_t *payload, size_t length, ConData *data);
int ReadMemData(const uint8_t *payload, size_t length, MemData *data);
int ReadBluetoothRegisterData(const uint8_t *payload, size_t length, BluetoothRegisterData *data);
int ReadBluetoothConnectData(const uint8_t *payload, size_t length, BluetoothConnectData *data);
int ReadResultData(const uint8_t *payload, size_t length, ResultData *data);

#ifdef __cplusplus
}
#endif
