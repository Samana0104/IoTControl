#pragma once

#include <main.h>

// USART별 수신 버퍼 크기 (F411의 USART1/2/6 칸이 미리 잡혀 있음)
#define SUSART_RX_BUFFER_SIZE 128

// 수신 시작 (DMA Circular + IDLE 감지, CubeMX에서 RX DMA Circular / USARTx global interrupt 필요)
// 이미 시작한 UART나 F411에 없는 UART면 false
bool SUsartBegin(UART_HandleTypeDef *huart);

// 받은 바이트 하나 꺼냄, 없거나 Begin 안 했으면 false
bool SUsartReadByte(UART_HandleTypeDef *huart, uint8_t *byte);

// 지금까지 받은 바이트 모두 버림
void SUsartFlush(UART_HandleTypeDef *huart);

// 블로킹 전송 (타임아웃은 보레이트 기준으로 계산, Begin 없이도 사용 가능)
void SUsartWrite(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t length);
void SUsartPrintf(UART_HandleTypeDef *huart, const char *format, ...) __attribute__((format(printf, 2, 3)));
void SUsartVPrintf(UART_HandleTypeDef *huart, const char *format, va_list args);
