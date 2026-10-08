#pragma once

// 디버그 빌드 (릴리즈에서는 주석 처리)
#define DEBUG_BUILD

// 로그 컴파일 레벨 (SLog.h의 SLOG_LEVEL_NONE/ERROR/WARN/INFO/DEBUG)
#ifdef DEBUG_BUILD
#define SLOG_LEVEL SLOG_LEVEL_DEBUG
#else
#define SLOG_LEVEL SLOG_LEVEL_INFO
#endif

// SData(플래시) 슬롯 주소, 슬롯 크기 = 1(magic) + 데이터 크기, 전체 SDATA_SIZE 안에 배치
#define SDATA_ADDR_MEMBER 0 // MemData, 1 + MEM_DATA_SIZE 바이트
