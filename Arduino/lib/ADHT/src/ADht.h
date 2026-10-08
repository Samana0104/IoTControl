#pragma once

#include <Arduino.h>

// DHT11 온습도 센서 (라이브러리 없이 정수만 사용: float 연산 라이브러리가 링크되지 않게)
// 신호선 1개, 풀업 필요 (INPUT_PULLUP 사용)
class ADht final
{
  public:
    explicit ADht(uint8_t pin)
        : pin(pin)
    {
    }

    ADht() = delete;
    ADht(const ADht &) = delete;
    ADht &operator=(const ADht &) = delete;

    void Begin();

    // 정수 ℃, %를 읽음. 실패(응답 없음, 체크섬 오류)면 false
    // DHT11은 1초에 한 번까지만 읽을 수 있어서 READ_INTERVAL_MS 안에 다시 부르면 마지막 결과를 돌려줌
    bool Read(uint8_t &temperature, uint8_t &humidity);

  private:
    bool ReadSensor();

  private:
    static constexpr uint32_t READ_INTERVAL_MS = 2000;
    // 시작 신호: MCU가 18ms 이상 LOW
    static constexpr uint8_t START_LOW_MS = 20;
    // 비트 HIGH 길이: 0 = 26~28us, 1 = 70us
    static constexpr unsigned long ONE_THRESHOLD_US = 50;
    static constexpr unsigned long PULSE_TIMEOUT_US = 1000;
    static constexpr uint8_t DATA_BITS = 40;
    static constexpr uint8_t DATA_BYTES = 5;

    uint8_t pin;
    bool hasRead = false;
    bool lastValid = false;
    uint32_t lastReadMs = 0;
    uint8_t lastTemperature = 0;
    uint8_t lastHumidity = 0;
};
