#pragma once

#include <main.h>

// LCD1602(HD44780 호환) + PCF8574 I2C 백팩, LCD 하나를 내부에서 관리.
// 백팩 배선: P0=RS, P1=RW, P2=EN, P3=백라이트(active HIGH), P4~P7=D4~D7.
// 다른 핀 배치/백라이트 극성의 백팩은 SClcd.c의 매핑을 맞춰야 함.
// I2C는 100kHz 이하 사용. 모든 함수는 블로킹이며 메인에서만 호출.
#define SCLCD_COLUMNS 16
#define SCLCD_ROWS 2
#define SCLCD_DEFAULT_ADDRESS 0x27
#define SCLCD_PRINT_SIZE (SCLCD_COLUMNS * SCLCD_ROWS + 1)

// 먼저 MX_I2Cx_Init() 호출. address는 시프트하지 않은 7비트 주소.
// PCF8574: 0x20~0x27, PCF8574A: 0x38~0x3F (백팩의 A0~A2 설정에 따름).
// 성공하면 화면을 지우고 표시/백라이트 ON, 커서/깜빡임 OFF로 시작.
bool SClcdInit(I2C_HandleTypeDef *hi2c, uint8_t address);

bool SClcdClear(void);
bool SClcdHome(void);

// 0부터 시작: column=0~15, row=0~1.
bool SClcdSetCursor(uint8_t column, uint8_t row);

// 줄바꿈/자동 줄 이동 없음. 각 줄 시작은 SetCursor로 지정.
bool SClcdWriteChar(char character);
bool SClcdWriteString(const char *text);

// 최대 32바이트. 결과가 더 길면 출력하지 않고 false 반환.
bool SClcdPrintf(const char *format, ...) __attribute__((format(printf, 1, 2)));

bool SClcdSetBacklight(bool enabled);
bool SClcdSetDisplay(bool enabled);

// 미초기화/잘못된 인수/I2C 오류 시 false.
// 전송 도중 I2C 오류가 나면 4비트 동기 복구를 위해 Init을 다시 호출해야 함.
