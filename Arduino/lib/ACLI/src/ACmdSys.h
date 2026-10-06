#pragma once

#include <Arduino.h>

// sys <info|mem|log|reset> ...
void CmdSys(Print &out, const char *args);
