#pragma once

#include <Arduino.h>

// gpio <mode|write|read|list> ...
void CmdGpio(Print &out, const char *args);
