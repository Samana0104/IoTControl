#pragma once

#include <Arduino.h>

class AWiFi;

// gpio <mode|write|read|list> ...
void CmdGpio(Print &out, const char *args);

// wifi <status|scan|connect|disconnect|autoconnect|init|baud> ...
// 사용 전 BindWiFi()로 모듈 연결
void BindWiFi(AWiFi &wifi);
void CmdWiFi(Print &out, const char *args);
