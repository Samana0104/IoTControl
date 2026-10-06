#pragma once

#include <main.h>

// ZS-040 (HC-05) 블루투스 시리얼 모듈 (보드에 1개, 상태는 드라이버 내부에서 관리)
// 데이터 모드: 기본 9600bps, 페어링된 기기와 투명 전송
// AT 모드: EN(KEY) 핀 HIGH 상태로 전원 인가 시 38400bps, 명령 끝은 "\r\n"

#define SZS040_LINE_SIZE 64
#define SZS040_AT_TIMEOUT_MS 1000

// 모듈이 연결된 UART 연결 + 수신 시작, STATE 안 쓰면 statePort에 NULL
bool SZS040Init(UART_HandleTypeDef *huart, GPIO_TypeDef *statePort, uint16_t statePin);

// STATE 핀으로 페어링 연결 여부 확인 (STATE 미사용이면 항상 false)
bool SZS040IsConnected(void);

// 받은 바이트 하나 꺼냄, 없으면 false
bool SZS040ReadByte(uint8_t *byte);

// 받은 바이트를 줄 단위로 모아 한 줄이 완성되면 반환 ("\r", "\n" 제외), 아직이면 NULL
// 반환된 문자열은 다음 ReadLine 호출 전까지만 유효
const char *SZS040ReadLine(void);

void SZS040Write(const uint8_t *data, uint16_t length);
void SZS040Printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

// AT 모드 전용: "AT+NAME?" 처럼 보내면 "\r\n"은 자동으로 붙임
// OK면 true, ERROR나 타임아웃이면 false
// response가 있으면 OK 전에 온 응답 줄(예: "+NAME:HC-05")을 복사 (블로킹)
bool SZS040SendAT(const char *command, char *response, uint16_t responseSize);
