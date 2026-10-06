#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "IoTProtocol.h"

typedef struct _SIotProtocolIo
{
    bool (*readByte)(uint8_t *byte);
    bool (*write)(const uint8_t *data, uint16_t length);
    uint32_t (*getTick)(void);
} SIotProtocolIo;

// 수신 버퍼는 콜백이 실행되는 동안만 유효하며 길이만큼 사용해야 함.
typedef void (*SIotPacketHandler)(uint8_t cmd, const uint8_t *data, uint8_t length);
// true는 UART DATA 전송 완료. 서버 처리 완료를 뜻하는 ACK는 프로토콜에 없음.
typedef void (*SIotSendHandler)(uint8_t cmd, bool success);

// 메인 루프에서만 호출. 하나의 HC-06 연결을 정적 버퍼로 관리.
void SIotProtocolInit(const SIotProtocolIo *io, SIotPacketHandler packetHandler, SIotSendHandler sendHandler);
void SIotProtocolUpdate(void);
bool SIotProtocolIsBusy(void);

// 본문을 내부 버퍼로 복사하고 OK를 보냄. RQ 대기는 Update에서 처리.
// 길이/명령 오류, 처리 중, UART 전송 실패이면 false.
bool SIotProtocolSendPacket(uint8_t cmd, const void *data, uint16_t length);
