#include "ACmdMember.h"
#include "ACommand.h"

#include <AData.h>
#include <ADefine.h>
#include <string.h>

// ---------------------------------------------------------------------------
// member <command>
// ---------------------------------------------------------------------------

static void MemberSet(Print &out, const char *args)
{
    char id[MEM_ID_SIZE + 1];
    char pw[MEM_PW_SIZE + 1];

    const char *p = NextToken(args, id, sizeof(id));
    bool valid = p != nullptr && id[0] != '\0' && NextToken(p, pw, sizeof(pw)) != nullptr && pw[0] != '\0';

    if (!valid)
    {
        out.println(F("usage: member set <id> <pw>  (id <= 8, pw <= 64)"));
        memset(pw, 0, sizeof(pw));
        return;
    }

    // 고정 길이 필드, 남는 칸은 '\0' (서버는 strnlen으로 길이 판단)
    MemData member = {};
    memcpy(member.id, id, strlen(id));
    memcpy(member.pw, pw, strlen(pw));

    if (SaveData(DATA_ADDR_MEMBER, reinterpret_cast<const uint8_t *>(&member), sizeof(member)))
    {
        out.print(F("saved: "));
        out.println(id);
    }
    else
    {
        out.println(F("save failed"));
    }

    memset(pw, 0, sizeof(pw));
    memset(&member, 0, sizeof(member));
}

static void MemberShow(Print &out, const char *args)
{
    (void)args;

    MemData member;
    if (!LoadData(DATA_ADDR_MEMBER, reinterpret_cast<uint8_t *>(&member), sizeof(member)))
    {
        out.println(F("no member saved"));
        return;
    }

    out.print(F("id: "));
    out.write(member.id, strnlen(member.id, MEM_ID_SIZE));
    out.println();

    // pw는 길이만 표시
    out.print(F("pw: ("));
    out.print(strnlen(member.pw, MEM_PW_SIZE));
    out.println(F(" chars)"));

    memset(&member, 0, sizeof(member));
}

static void MemberClear(Print &out, const char *args)
{
    (void)args;

    ClearData(DATA_ADDR_MEMBER, sizeof(MemData));
    out.println(F("cleared"));
}

static const ACommand memberCommands[] = {
    {"set", MemberSet},
    {"show", MemberShow},
    {"clear", MemberClear},
};

void CmdMember(Print &out, const char *args)
{
    Dispatch(out, args, F("member"), memberCommands, sizeof(memberCommands) / sizeof(memberCommands[0]));
}
