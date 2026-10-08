#include "ACLI.h"
#include "ACmdSys.h"
#include "ACmdGpio.h"
#include "ACmdWiFi.h"
#include "ACmdMember.h"

#include <string.h>

const ACommand ACLI::commands[] = {
    {"sys", CmdSys},
    {"gpio", CmdGpio},
    {"wifi", CmdWiFi},
    {"member", CmdMember},
};

const uint8_t ACLI::commandCount = sizeof(commands) / sizeof(commands[0]);

void ACLI::ReadSerial()
{
    while (serial.available() > 0)
    {
        char c = (char)serial.read();

        // \r\n 으로 들어오면 \n 은 이미 처리한 줄바꿈이라 무시
        if (c == '\n' && lastWasCR)
        {
            lastWasCR = false;
            continue;
        }
        lastWasCR = (c == '\r');

        // 백스페이스 (터미널에 따라 0x08 또는 0x7F)
        if (c == '\b' || c == 0x7F)
        {
            if (bufferLen > 0 && !overflow)
            {
                --bufferLen;
                serial.print(F("\b \b"));
            }
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            serial.println();
            buffer[bufferLen] = '\0';
            if (overflow)
            {
                serial.println(F("command too long"));
            }
            else if (bufferLen > 0)
            {
                ParseCommand(buffer);
            }
            bufferLen = 0;
            overflow = false;
            continue;
        }

        serial.write(c);

        // '\0' 자리 1바이트 남겨둠, 넘치면 줄바꿈까지 버림
        if (bufferLen < BUFFER_SIZE - 1)
        {
            buffer[bufferLen++] = c;
        }
        else
        {
            overflow = true;
        }
    }
}

void ACLI::ParseCommand(const char* command)
{
    const char* space = strchr(command, ' ');
    size_t len = space ? (size_t)(space - command) : strlen(command);
    const char* args = space ? space + 1 : "";

    for (uint8_t i = 0; i < commandCount; ++i)
    {
        const ACommand &cmd = commands[i];
        if (strlen(cmd.name) == len && strncasecmp(cmd.name, command, len) == 0)
        {
            cmd.handler(serial, args);
            return;
        }
    }

    PrintCommandList();
}

void ACLI::PrintCommandList()
{
    serial.println(F("commands:"));
    for (uint8_t i = 0; i < commandCount; ++i)
    {
        serial.print(F("  "));
        serial.println(commands[i].name);
    }
}
