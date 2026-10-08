#pragma once

#include <Arduino.h>

// 디버그 빌드: Serial CLI 활성화 (릴리즈에서는 주석 처리)
#define DEBUG_CLI

// 로그 컴파일 레벨 (ALog.h의 ALOG_LEVEL_NONE/ERROR/WARN/INFO/DEBUG)
#ifdef DEBUG_CLI
#define ALOG_LEVEL ALOG_LEVEL_DEBUG
#else
#define ALOG_LEVEL ALOG_LEVEL_INFO
#endif

// 저장소 루트 common/ 의 공용 패킷 프로토콜 (Raspberry5, STM32, PC와 같은 파일)
// 구현(.c)은 platformio.ini의 build_src_filter로 빌드에 포함
#include "../../../../common/IoTPacketCodec.h"

// AData(EEPROM) 슬롯 주소, 슬롯 크기 = 1(magic) + 데이터 크기
#define DATA_ADDR_MEMBER 0 // MemData, 1 + MEM_DATA_SIZE 바이트
