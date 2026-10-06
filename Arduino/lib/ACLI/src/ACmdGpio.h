#pragma once

#include <Arduino.h>

// gpio <mode|write|read|pins> ...
void CmdGpio(Print &out, const char *args);
