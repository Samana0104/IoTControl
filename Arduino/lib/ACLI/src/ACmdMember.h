#pragma once

#include <Arduino.h>

// member <set|show|clear> ...   서버 로그인 계정 (EEPROM 저장)
void CmdMember(Print &out, const char *args);
