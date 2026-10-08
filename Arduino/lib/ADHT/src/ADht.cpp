#include "ADht.h"

void ADht::Begin()
{
    pinMode(pin, INPUT_PULLUP);
}

bool ADht::Read(uint8_t &temperature, uint8_t &humidity)
{
    uint32_t now = millis();

    if (!hasRead || now - lastReadMs >= READ_INTERVAL_MS)
    {
        hasRead = true;
        lastReadMs = now;
        lastValid = ReadSensor();
    }
    temperature = lastTemperature;
    humidity = lastHumidity;
    return lastValid;
}

// 응답: 센서가 LOW 80us → HIGH 80us, 이후 40비트 = 각 비트 LOW 50us + HIGH(0: 26~28us, 1: 70us)
// 데이터: 습도 정수, 습도 소수, 온도 정수, 온도 소수, 체크섬(앞 4바이트 합)
// pulseIn은 CPU 사이클로 재므로 인터럽트를 끈 동안에도 정확함 (읽는 데 약 4ms)
bool ADht::ReadSensor()
{
    uint8_t data[DATA_BYTES] = {0};
    bool ok;

    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(START_LOW_MS);
    pinMode(pin, INPUT_PULLUP);

    noInterrupts();
    // 응답 HIGH 80us (pulseIn이 그 앞의 LOW가 끝나기를 기다림)
    ok = pulseIn(pin, HIGH, PULSE_TIMEOUT_US) != 0;
    for (uint8_t bit = 0; ok && bit < DATA_BITS; ++bit)
    {
        unsigned long width = pulseIn(pin, HIGH, PULSE_TIMEOUT_US);

        ok = width != 0;
        data[bit / 8] <<= 1;
        if (width > ONE_THRESHOLD_US)
        {
            data[bit / 8] |= 1;
        }
    }
    interrupts();

    if (!ok || (uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
    {
        return false;
    }
    lastHumidity = data[0];
    lastTemperature = data[2];
    return true;
}
