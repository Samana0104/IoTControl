#include "SCommand.h"
#include "SCLI.h"

const char *SCommandNextToken(const char *p, char *token, uint8_t size)
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
    return truncated ? NULL : p;
}

void SCommandDispatch(const char *args, const char *group, const SSubCommand *subs, uint8_t count)
{
    char token[SCOMMAND_TOKEN_SIZE];
    const char *p = SCommandNextToken(args, token, SCOMMAND_TOKEN_SIZE);

    if (p != NULL && token[0] != '\0')
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            if (strcasecmp(token, subs[i].name) == 0)
            {
                while (*p == ' ')
                {
                    ++p;
                }
                subs[i].handler(p);
                return;
            }
        }
    }

    SCLIPrintf("usage: %s <command>\r\n", group);
    for (uint8_t i = 0; i < count; ++i)
    {
        SCLIPrintf("  %s\r\n", subs[i].name);
    }
}
