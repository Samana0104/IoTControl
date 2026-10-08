#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "IoTProtocol.h"

// 메인 루프에서만 호출. 블루투스(SZS040) 연결 하나를 정적 버퍼로 관리.
// SZS040Init 이후에 호출해야 함 (송수신은 SZS040ReadByte/SZS040Write로 직접 함)
// CRC 검사를 통과한 프레임은 SIotProtocol.c의 PACKET_TABLE에서 cmd별 처리 함수로 넘어감
void SIotProtocolInit(void);
void SIotProtocolUpdate(void);
// 프레임 수신 중이거나 깨진 바이트를 버리는 중이면 true.
bool SIotProtocolIsBusy(void);

// 8바이트 헤더 + 본문 프레임(CRC 포함)을 만들어 바로 전송.
// 길이/명령 오류, 수신 중, UART 전송 실패이면 false.
bool SIotProtocolSendPacket(uint16_t cmd, const void *data, uint16_t length);
