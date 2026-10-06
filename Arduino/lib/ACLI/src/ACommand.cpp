#include "ACommand.h"

#include <string.h>

// 공백 전까지 잘라서 token에 복사하고, 다음 위치를 반환
// token 크기를 넘으면 nullptr 반환
const char *NextToken(const char *p, char *token, uint8_t size)
{
    while (*p == ' ')
    {
        ++p;
    }

    uint8_t n = 0;
    bool truncated = false;
    while (*p != '\0' && *p != ' ')
    {
        if (n < size - 1)
        {
            token[n++] = *p;
        }
        else
        {
            truncated = true;
        }
        ++p;
    }
    token[n] = '\0';
    return truncated ? nullptr : p;
}

// 첫 토큰으로 하위 커맨드를 찾아 나머지 인자를 넘김, 없으면 하위 목록 출력
void Dispatch(Print &out, const char *args, const __FlashStringHelper *group, const ACommand *subs, uint8_t count)
{
    char token[TOKEN_SIZE];
    const char *p = NextToken(args, token, TOKEN_SIZE);

    if (p != nullptr && token[0] != '\0')
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            if (strcasecmp(token, subs[i].name) == 0)
            {
                while (*p == ' ')
                {
                    ++p;
                }
                subs[i].handler(out, p);
                return;
            }
        }
    }

    out.print(F("usage: "));
    out.print(group);
    out.println(F(" <command>"));
    for (uint8_t i = 0; i < count; ++i)
    {
        out.print(F("  "));
        out.println(subs[i].name);
    }
}
