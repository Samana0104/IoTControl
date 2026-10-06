#pragma once

#include <Arduino.h>

// GPIO
void CmdPinMode(Print &out, const char *args); // pinmode <pin> <in|out|pullup>
void CmdWrite(Print &out, const char *args);   // write <pin> <0|1|low|high>
void CmdRead(Print &out, const char *args);    // read <pin>
void CmdPins(Print &out, const char *args);    // pins
