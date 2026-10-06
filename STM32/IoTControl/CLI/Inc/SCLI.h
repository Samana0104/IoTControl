#pragma once

#include <main.h>

// USART2 수신 시작 (DMA Circular + IDLE 감지, MX_USART2_UART_Init 이후 호출)
void SCLIInit(void);

// 수신 플래그가 섰을 때만 받은 바이트를 처리 (메인 루프에서 매번 호출)
void SCLIUpdate(void);

// CLI 응답 출력 (printf 형식, 줄바꿈은 직접 넣음, 블로킹 전송)
void SCLIPrintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
