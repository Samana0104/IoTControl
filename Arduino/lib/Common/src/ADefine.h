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
