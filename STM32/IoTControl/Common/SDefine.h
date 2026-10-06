#pragma once

// 디버그 빌드 (릴리즈에서는 주석 처리)
#define DEBUG_BUILD

// 로그 컴파일 레벨 (SLog.h의 SLOG_LEVEL_NONE/ERROR/WARN/INFO/DEBUG)
#ifdef DEBUG_BUILD
#define SLOG_LEVEL SLOG_LEVEL_DEBUG
#else
#define SLOG_LEVEL SLOG_LEVEL_INFO
#endif
