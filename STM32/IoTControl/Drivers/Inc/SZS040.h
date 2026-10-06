#pragma once

#include <main.h>

// ZS-040 (HC-06) 블루투스 시리얼 모듈 (슬레이브 전용, 보드에 1개, 상태는 드라이버 내부에서 관리)
// 데이터 모드: 페어링된 기기와 투명 전송, 기본 9600bps

#define SZS040_LINE_SIZE 64

// AT 응답 대기: 전체 최대 시간 / 마지막 바이트 이후 이만큼 조용하면 응답 끝으로 봄
#define SZS040_AT_TIMEOUT_MS 1500
#define SZS040_AT_IDLE_MS 200

// 모듈이 연결된 UART 연결 + 수신 시작, STATE 안 쓰면 statePort에 NULL
bool SZS040Init(UART_HandleTypeDef *huart, GPIO_TypeDef *statePort, uint16_t statePin);

// MCU 쪽 UART 보레이트만 변경 (모듈 보레이트를 이미 알고 있을 때 맞추는 용도)
bool SZS040SetUartBaud(uint32_t baud);
uint32_t SZS040GetUartBaud(void);

// STATE 핀으로 페어링 연결 여부 확인 (STATE 미사용이면 항상 false)
bool SZS040IsConnected(void);

// 받은 바이트 하나 꺼냄, 없으면 false
bool SZS040ReadByte(uint8_t *byte);

// 받은 바이트를 줄 단위로 모아 한 줄이 완성되면 반환 ("\r", "\n" 제외), 아직이면 NULL
// 반환된 문자열은 다음 ReadLine 호출 전까지만 유효
const char *SZS040ReadLine(void);

void SZS040Write(const uint8_t *data, uint16_t length);
void SZS040Printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

// AT 명령을 그대로 보내고 응답을 받음 (줄바꿈 안 붙임, 블로킹)
bool SZS040SendAT(const char *command, char *response, uint16_t responseSize);

// ---------------------------------------------------------------------------
// 자주 쓰는 AT 명령 (페어링 안 된 상태에서만, 블로킹, 성공하면 true)
// HC-06은 설정값 조회가 안 되고 버전만 조회 가능
// ---------------------------------------------------------------------------

// "AT" -> "OK"
bool SZS040Test(void);

// "AT+VERSION" -> "OKlinvorV1.8", 앞의 "OK"를 떼고 돌려줌
bool SZS040GetVersion(char *version, uint16_t size);

// "AT+NAMExxx" -> "OKsetname" (최대 20자)
bool SZS040SetName(const char *name);

// "AT+PIN1234" -> "OKsetPIN" (숫자 4자리)
bool SZS040SetPin(const char *pin);

// "AT+BAUD4" -> "OK9600", 1200/2400/4800/9600/19200/38400/57600/115200만 가능
// 모듈이 OK 응답하면 MCU 쪽 UART 보레이트도 같이 바꿈
bool SZS040SetBaud(uint32_t baud);
