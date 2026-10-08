#pragma once

#include <main.h>
#include "IoTFirmware.h"

// 서버 → 장치 펌웨어 업데이트 (배치와 순서는 common/IoTFirmware.h)
// REQ_FW_BEGIN: 대기 영역(섹터 5) 지우기, 1~2초 걸림
void SPacketFirmwareBeginReceive(const uint8_t *payload, uint16_t length);
// REQ_FW_CHUNK: 오프셋 0부터 순서대로 대기 영역에 씀. 이미 쓴 오프셋의 재전송은 쓰지 않고 성공 응답
void SPacketFirmwareChunkReceive(const uint8_t *payload, uint16_t length);
// REQ_FW_END: 이미지 CRC32·형식 확인 후 대기 헤더를 쓰고, ACK를 보낸 뒤 재부팅 예약
void SPacketFirmwareEndReceive(const uint8_t *payload, uint16_t length);

// 메인 루프에서 호출. REQ_FW_END 성공 뒤 ACK가 블루투스로 나갈 시간을 두고 재부팅 (부트로더가 설치)
void SPacketFirmwareUpdate(void);

// 지금 실행 중인 앱의 버전 정보 (version = CMake FIRMWARE_VERSION), 링커가 앱 시작 + 0x200에 둠
extern const FirmwareInfo firmwareInfo;
