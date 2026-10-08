#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "IoTProtocol.h"

// 수신 버퍼는 콜백이 실행되는 동안만 유효하며 길이만큼 사용해야 함. CRC 검사를 통과한 프레임만 전달.
typedef void (*SIotPacketHandler)(uint16_t cmd, const uint8_t *data, uint16_t length);
// true는 UART로 프레임 전체를 보냈다는 뜻. 서버 처리 결과는 ACK 프레임으로 따로 수신.
typedef void (*SIotSendHandler)(uint16_t cmd, bool success);

// 메인 루프에서만 호출. 블루투스(SZS040) 연결 하나를 정적 버퍼로 관리.
// SZS040Init 이후에 호출해야 함 (송수신은 SZS040ReadByte/SZS040Write로 직접 함)
void SIotProtocolInit(SIotPacketHandler packetHandler, SIotSendHandler sendHandler);
void SIotProtocolUpdate(void);
// 프레임 수신 중이거나 깨진 바이트를 버리는 중이면 true.
bool SIotProtocolIsBusy(void);

// 8바이트 헤더 + 본문 프레임(CRC 포함)을 만들어 바로 전송.
// 길이/명령 오류, 수신 중, UART 전송 실패이면 false.
bool SIotProtocolSendPacket(uint16_t cmd, const void *data, uint16_t length);
