#pragma once

#include <main.h>

// CLI로 쓸 UART 연결 + 수신 시작
void SCLIInit(UART_HandleTypeDef *huart);

// 받은 바이트를 처리 (메인 루프에서 매번 호출)
void SCLIUpdate(void);

// CLI 응답 출력 (printf 형식, 줄바꿈은 직접 넣음, 블로킹 전송)
void SCLIPrintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
