#pragma once

#include <SoftwareSerial.h>
#include <Arduino.h>

class ACLI
{
public:
    ACLI();
    ~ACLI();

    void ParseCommand(const char* command);
    
    ACLI(const ACLI&) = delete;
    ACLI& operator=(const ACLI&) = delete;
    ACLI(ACLI&&) = delete;
    ACLI& operator=(ACLI&&) = delete;
private:
    void begin();
    void end();
    
public:

private:
    SoftwareSerial serial;
};