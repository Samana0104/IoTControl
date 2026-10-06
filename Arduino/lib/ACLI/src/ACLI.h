#pragma once

#include <Arduino.h>

class ACLI final
{
  public:
    explicit ACLI(Stream &stream) : serial(stream) {}

    void ReadSerial();
    void ParseCommand(const char *command);

    ACLI(const ACLI &) = delete;
    ACLI &operator=(const ACLI &) = delete;
    ACLI(ACLI &&) = delete;
    ACLI &operator=(ACLI &&) = delete;

  private:
    void PrintCommandList();

  private:
    using Handler = void (*)(Print &out, const char *args);

    struct Command
    {
        const char *name;
        Handler handler;
    };

    static const Command commands[];
    static const uint8_t commandCount;

  private:
    static constexpr uint8_t BUFFER_SIZE = 128;

    Stream &serial;

    char buffer[BUFFER_SIZE];
    uint8_t bufferLen = 0;

    bool overflow = false;
    bool lastWasCR = false;
};
