#include "SCmdMember.h"
#include "SCommand.h"
#include "SCLI.h"
#include "SData.h"
#include "SDefine.h"
#include "IoTPacket.h"

// ---------------------------------------------------------------------------
// member <command>
// ---------------------------------------------------------------------------

static void MemberSet(const char *args)
{
    char id[MEM_ID_SIZE + 1];
    char pw[MEM_PW_SIZE + 1];

    const char *p = SCommandNextToken(args, id, sizeof(id));
    bool valid = p != NULL && id[0] != '\0' && SCommandNextToken(p, pw, sizeof(pw)) != NULL && pw[0] != '\0';

    if (!valid)
    {
        SCLIPrintf("usage: member set <id> <pw>  (id <= %u, pw <= %u)\r\n", MEM_ID_SIZE, MEM_PW_SIZE);
        memset(pw, 0, sizeof(pw));
        return;
    }

    // 고정 길이 필드, 남는 칸은 '\0' (서버는 strnlen으로 길이 판단)
    MemData member = {0};
    memcpy(member.id, id, strlen(id));
    memcpy(member.pw, pw, strlen(pw));

    if (SDataSave(SDATA_ADDR_MEMBER, (const uint8_t *)&member, sizeof(member)))
    {
        SCLIPrintf("saved: %s\r\n", id);
    }
    else
    {
        SCLIPrintf("save failed\r\n");
    }

    memset(pw, 0, sizeof(pw));
    memset(&member, 0, sizeof(member));
}

static void MemberShow(const char *args)
{
    (void)args;

    MemData member;
    if (!SDataLoad(SDATA_ADDR_MEMBER, (uint8_t *)&member, sizeof(member)))
    {
        SCLIPrintf("no member saved\r\n");
        return;
    }

    SCLIPrintf("id: %.*s\r\n", (int)strnlen(member.id, MEM_ID_SIZE), member.id);
    // pw는 길이만 표시
    SCLIPrintf("pw: (%u chars)\r\n", (unsigned int)strnlen(member.pw, MEM_PW_SIZE));

    memset(&member, 0, sizeof(member));
}

static void MemberClear(const char *args)
{
    (void)args;

    SCLIPrintf(SDataClear(SDATA_ADDR_MEMBER, sizeof(MemData)) ? "cleared\r\n" : "clear failed\r\n");
}

static const SCommand memberCommands[] = {
    {"set", MemberSet},
    {"show", MemberShow},
    {"clear", MemberClear},
};

void SCmdMember(const char *args)
{
    SCommandDispatch(args, "member", memberCommands, SCOMMAND_COUNT(memberCommands));
}
