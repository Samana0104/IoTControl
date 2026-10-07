#pragma once


#include <Arduino.h>
#include <DHT.h>

class ADht final
{
  public:
    explicit ADht(uint8_t pin)
        : dht(pin, DHT11)
    {
    }

    ADht() = delete;
    ADht(const ADht &) = delete;
    ADht &operator=(const ADht &) = delete;

    void Begin();
    float GetTemperature();
    float GetHumidity();


  private:
    DHT dht;
};