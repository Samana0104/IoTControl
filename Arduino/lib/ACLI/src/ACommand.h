#pragma once

#include <Arduino.h>

// 커맨드 파일(ACmdXxx.cpp)끼리 공유하는 파싱/분기 헬퍼

constexpr uint8_t TOKEN_SIZE = 12;

using SubHandler = void (*)(Print &out, const char *args);

struct SubCommand
{
    const char *name;
    SubHandler handler;
};

// 공백 전까지 잘라서 token에 복사하고, 다음 위치를 반환
// token 크기를 넘으면 nullptr 반환
const char *NextToken(const char *p, char *token, uint8_t size);

// 첫 토큰으로 하위 커맨드를 찾아 나머지 인자를 넘김, 없으면 하위 목록 출력
void Dispatch(Print &out, const char *args, const __FlashStringHelper *group,
              const SubCommand *subs, uint8_t count);
