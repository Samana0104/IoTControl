#pragma once

#include <Arduino.h>

class AWiFi;

// wifi <status|scan|connect|disconnect|autoconnect|init|baud|login> ...
// 사용 전 BindWiFi()로 모듈 연결
void BindWiFi(AWiFi &wifi);
void CmdWiFi(Print &out, const char *args);
