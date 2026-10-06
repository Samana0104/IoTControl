#pragma once

#include <main.h>
#include <SDefine.h>

// 로그 레벨 (숫자가 클수록 자세함)
#define SLOG_LEVEL_NONE 0
#define SLOG_LEVEL_ERROR 1
#define SLOG_LEVEL_WARN 2
#define SLOG_LEVEL_INFO 3
#define SLOG_LEVEL_DEBUG 4

// 컴파일 레벨: 이보다 자세한 로그는 코드에서 아예 빠짐 (SDefine.h에서 지정)
#ifndef SLOG_LEVEL
#define SLOG_LEVEL SLOG_LEVEL_INFO
#endif

// 로그 한 줄 최대 길이 (머리말 포함, 넘치면 잘림)
#define SLOG_LINE_SIZE 128

// 런타임 레벨: 컴파일 레벨 안에서 추가로 거름
void SLogSetLevel(uint8_t level);
uint8_t SLogGetLevel(void);

// "[ms][L] " 머리말 + printf 형식 본문 + 줄바꿈 출력 (블로킹 전송, ISR에서 호출 금지)
void SLogWrite(uint8_t level, const char *format, ...) __attribute__((format(printf, 2, 3)));

// 사용법: SLOG_INFO("baud=%lu ok", baud);
#if SLOG_LEVEL >= SLOG_LEVEL_ERROR
#define SLOG_ERROR(format, ...) SLogWrite(SLOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#else
#define SLOG_ERROR(format, ...) do {} while (0)
#endif

#if SLOG_LEVEL >= SLOG_LEVEL_WARN
#define SLOG_WARN(format, ...) SLogWrite(SLOG_LEVEL_WARN, format, ##__VA_ARGS__)
#else
#define SLOG_WARN(format, ...) do {} while (0)
#endif

#if SLOG_LEVEL >= SLOG_LEVEL_INFO
#define SLOG_INFO(format, ...) SLogWrite(SLOG_LEVEL_INFO, format, ##__VA_ARGS__)
#else
#define SLOG_INFO(format, ...) do {} while (0)
#endif

#if SLOG_LEVEL >= SLOG_LEVEL_DEBUG
#define SLOG_DEBUG(format, ...) SLogWrite(SLOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#else
#define SLOG_DEBUG(format, ...) do {} while (0)
#endif
