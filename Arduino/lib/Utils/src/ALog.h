#pragma once

#include <Arduino.h>
#include <ADefine.h>

// 로그 레벨 (숫자가 클수록 자세함)
#define ALOG_LEVEL_NONE 0
#define ALOG_LEVEL_ERROR 1
#define ALOG_LEVEL_WARN 2
#define ALOG_LEVEL_INFO 3
#define ALOG_LEVEL_DEBUG 4

// 컴파일 레벨: 이보다 자세한 로그는 코드에서 아예 빠짐 (ADefine.h에서 지정)
#ifndef ALOG_LEVEL
#define ALOG_LEVEL ALOG_LEVEL_INFO
#endif

// 로그 출력 대상 연결 (출력 대상의 begin은 호출하는 쪽에서)
void BeginLog(Print &output);

// 런타임 레벨: 컴파일 레벨 안에서 추가로 거름
void SetLogLevel(uint8_t level);
uint8_t GetLogLevel();

// 레벨 통과 시 "[ms][L] " 머리말을 출력하고 출력 대상을 반환, 걸러지면 nullptr
Print *BeginLogLine(uint8_t level);

inline void PrintLogArgs(Print &)
{
}

template <typename T, typename... Rest>
void PrintLogArgs(Print &out, const T &value, const Rest &...rest)
{
    out.print(value);
    PrintLogArgs(out, rest...);
}

template <typename... Args>
void WriteLog(uint8_t level, const Args &...args)
{
    Print *out = BeginLogLine(level);
    if (out == nullptr)
    {
        return;
    }

    PrintLogArgs(*out, args...);
    out->println();
}

// 사용법: ALOG_INFO("baud=", baud, " ok");  첫 인자는 문자열 리터럴(F()로 플래시에 저장)
#if ALOG_LEVEL >= ALOG_LEVEL_ERROR
#define ALOG_ERROR(msg, ...) WriteLog(ALOG_LEVEL_ERROR, F(msg), ##__VA_ARGS__)
#else
#define ALOG_ERROR(msg, ...) do {} while (0)
#endif

#if ALOG_LEVEL >= ALOG_LEVEL_WARN
#define ALOG_WARN(msg, ...) WriteLog(ALOG_LEVEL_WARN, F(msg), ##__VA_ARGS__)
#else
#define ALOG_WARN(msg, ...) do {} while (0)
#endif

#if ALOG_LEVEL >= ALOG_LEVEL_INFO
#define ALOG_INFO(msg, ...) WriteLog(ALOG_LEVEL_INFO, F(msg), ##__VA_ARGS__)
#else
#define ALOG_INFO(msg, ...) do {} while (0)
#endif

#if ALOG_LEVEL >= ALOG_LEVEL_DEBUG
#define ALOG_DEBUG(msg, ...) WriteLog(ALOG_LEVEL_DEBUG, F(msg), ##__VA_ARGS__)
#else
#define ALOG_DEBUG(msg, ...) do {} while (0)
#endif
