#pragma once

#include <Arduino.h>
#include <SoftwareSerial.h>

// ESP8266(AT 1.7+ 펌웨어) 모듈 관리, 내부적으로 WiFiEspAT 사용
// 접속 정보는 ESP 플래시에 저장되고 재연결도 ESP가 직접 처리함
class AWiFi final
{
  public:
    AWiFi(uint8_t rxPin, uint8_t txPin) : serial(rxPin, txPin) {}

    AWiFi() = delete;
    AWiFi(const AWiFi &) = delete;
    AWiFi &operator=(const AWiFi &) = delete;
    AWiFi(AWiFi &&) = delete;
    AWiFi &operator=(AWiFi &&) = delete;

    bool Begin(long baudRate);
    bool SetModuleBaud(long baudRate);

    bool Connect(const char *ssid, const char *pass);
    void Disconnect();
    bool IsConnected();

    // 전원 인가 시 저장된 AP로 자동 접속 여부 (ESP 플래시에 저장)
    bool SetAutoConnect(bool enable);

    void PrintStatus(Print &out);
    void PrintScan(Print &out);

  private:
    static constexpr uint8_t SCAN_MAX = 5;

    SoftwareSerial serial;

    long baudRate = 0;
    bool ready = false;
};
