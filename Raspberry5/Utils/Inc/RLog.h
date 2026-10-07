#pragma once

#include <stdint.h>
#include <stdio.h>

// 로그 레벨 (숫자가 클수록 자세함)
#define RLOG_LEVEL_NONE 0
#define RLOG_LEVEL_ERROR 1
#define RLOG_LEVEL_WARN 2
#define RLOG_LEVEL_INFO 3
#define RLOG_LEVEL_DEBUG 4

// 컴파일 레벨: 이보다 자세한 로그는 코드에서 아예 빠짐 (CMake에서 -DRLOG_LEVEL=... 로 지정 가능)
#ifndef RLOG_LEVEL
#define RLOG_LEVEL RLOG_LEVEL_INFO
#endif

// 로그 한 줄 최대 길이 (머리말 포함, 넘치면 잘림)
#define RLOG_LINE_SIZE 512

// 로그 출력 대상 연결, 기본은 stdout. 프로그램 시작 시 한 번 호출 (실행 중 교체 시 이전 대상은 호출한 쪽이 닫음)
void RLogBegin(FILE *output);

// 런타임 레벨: 컴파일 레벨 안에서 추가로 거름
void RLogSetLevel(uint8_t level);
uint8_t RLogGetLevel(void);

// "[YYYY-MM-DD HH:MM:SS.mmm][L] " 머리말 + printf 형식 본문 + 줄바꿈 출력
// file이 NULL이 아니면 머리말 뒤에 "[파일:줄]"을 붙임 (ERROR/DEBUG 매크로가 전달)
// 여러 스레드에서 호출해도 한 줄씩 섞이지 않게 출력, 넘치면 끝을 "..."으로 표시
void RLogWrite(uint8_t level, const char *file, int line, const char *format, ...) __attribute__((format(printf, 4, 5)));

// 사용법: RLOG_INFO("client connected: ip=%s", ip);
// 꺼진 레벨은 if(0)으로 남겨 인자 형식 검사만 하고 코드는 생성되지 않음 (로그 전용 변수의 미사용 경고 방지)
#if RLOG_LEVEL >= RLOG_LEVEL_ERROR
#define RLOG_ERROR(format, ...) RLogWrite(RLOG_LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define RLOG_ERROR(format, ...) do { if(0) RLogWrite(RLOG_LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__); } while (0)
#endif

#if RLOG_LEVEL >= RLOG_LEVEL_WARN
#define RLOG_WARN(format, ...) RLogWrite(RLOG_LEVEL_WARN, NULL, 0, format, ##__VA_ARGS__)
#else
#define RLOG_WARN(format, ...) do { if(0) RLogWrite(RLOG_LEVEL_WARN, NULL, 0, format, ##__VA_ARGS__); } while (0)
#endif

#if RLOG_LEVEL >= RLOG_LEVEL_INFO
#define RLOG_INFO(format, ...) RLogWrite(RLOG_LEVEL_INFO, NULL, 0, format, ##__VA_ARGS__)
#else
#define RLOG_INFO(format, ...) do { if(0) RLogWrite(RLOG_LEVEL_INFO, NULL, 0, format, ##__VA_ARGS__); } while (0)
#endif

#if RLOG_LEVEL >= RLOG_LEVEL_DEBUG
#define RLOG_DEBUG(format, ...) RLogWrite(RLOG_LEVEL_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define RLOG_DEBUG(format, ...) do { if(0) RLogWrite(RLOG_LEVEL_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__); } while (0)
#endif
