#pragma once

#include <stddef.h>
#include <stdint.h>

// 서버가 패킷 하나를 Controller에 넘길 때 함께 주는 정보
typedef struct _RCtrlContext
{
    const char *label;    // 로그용 접속 표시 (IP 또는 BT 표시)
    const char *memberId; // 이 연결의 회원 ID (로그인/BT 바인딩으로 확인된 값)
    int fd;               // 이 연결의 세션 fd (같은 장치로 응답/제어할 때 RSessionSend에 사용)
} RCtrlContext;

// 서버의 패킷 테이블에 등록되는 Controller 수신 함수
// payload는 cmd에 맞는 길이로 이미 검사됨. 형식이 깨졌으면 -1 (서버가 연결을 끊음), 그 외는 0
typedef int (*RCtrlHandler)(const RCtrlContext *context, const uint8_t *payload, size_t length);
