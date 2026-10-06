#pragma once

#include <main.h>

#define SCOMMAND_TOKEN_SIZE 12

typedef void (*SCommandHandler)(const char *args);

typedef struct _SCommand
{
    const char *name;
    SCommandHandler handler;
} SCommand;

// 공백 전까지 잘라서 token에 복사하고 다음 위치 반환, token 크기를 넘으면 NULL
const char *SCommandNextToken(const char *p, char *token, uint8_t size);

// 첫 토큰으로 커맨드를 찾아 나머지 인자를 넘김, 없으면 목록 출력
void SCommandDispatch(const char *args, const char *group, const SCommand *commands, uint8_t count);

#define SCOMMAND_COUNT(commands) ((uint8_t)(sizeof(commands) / sizeof((commands)[0])))
