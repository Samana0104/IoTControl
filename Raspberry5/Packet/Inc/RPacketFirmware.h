#pragma once

#include "RSession.h"

#include <stddef.h>
#include <stdint.h>

// 장치 → 서버 ACK_FW_*: RPacketFirmwarePush가 기다리는 응답을 깨움 (기다리지 않는 ACK는 로그만 남김)
int RPacketFirmwareBeginReceiveAck(RSession *session, const uint8_t *payload, size_t length);
int RPacketFirmwareChunkReceiveAck(RSession *session, const uint8_t *payload, size_t length);
int RPacketFirmwareEndReceiveAck(RSession *session, const uint8_t *payload, size_t length);

// 서버 → STM32 펌웨어 전송: REQ_FW_BEGIN → REQ_FW_CHUNK(오프셋 0부터) → REQ_FW_END, 매번 ACK를 기다림.
// image는 CheckFirmwareImage를 통과해야 함. 끝날 때까지 호출한 스레드가 블록됨 (9600bps에서 50KB ≈ 1분)
// 0: 장치가 이미지를 검증하고 재부팅함 (부트로더가 설치), -1: 실패 (errno: EBUSY 다른 전송 중, EINVAL 이미지, ETIMEDOUT, EIO)
int RPacketFirmwarePush(int fd, const uint8_t *image, size_t size);
