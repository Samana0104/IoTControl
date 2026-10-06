#pragma once

#include <Arduino.h>

// ACLI 최상위 테이블과 각 커맨드 그룹(ACmdXxx.cpp)이 공유하는 커맨드 정의/파싱/분기

constexpr uint8_t TOKEN_SIZE = 12;

using CommandHandler = void (*)(Print &out, const char *args);

struct ACommand
{
    const char *name;
    CommandHandler handler;
};

// 공백 전까지 잘라서 token에 복사하고, 다음 위치를 반환
// token 크기를 넘으면 nullptr 반환
const char *NextToken(const char *p, char *token, uint8_t size);

// 첫 토큰으로 하위 커맨드를 찾아 나머지 인자를 넘김, 없으면 하위 목록 출력
void Dispatch(Print &out, const char *args, const __FlashStringHelper *group,
              const ACommand *subs, uint8_t count);
