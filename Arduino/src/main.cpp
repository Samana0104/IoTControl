#include "WiFiEsp.h"
#include <Arduino.h>
#include <DHT.h>
#include <ACLI.h>
#include <AIntervalMS.h>

// ESP8266 연결용 (SoftwareSerial)
const int RX_PIN = 10;
const int TX_PIN = 11;
const long BAUD_RATE = 115200;

static ACLI CLIHandler(Serial);

void loop()
{
    // 내부에서만 사용
    static constexpr uint32_t INTERVAL_MS_2Sec = 2000;
    static constexpr uint32_t INTERVAL_MS_5Sec = 5000;

    static IntervalMS interval2Sec(INTERVAL_MS_2Sec);
    static IntervalMS interval5Sec(INTERVAL_MS_5Sec);

    CLIHandler.ReadSerial();

    uint32_t currentTime = millis();

    if (interval2Sec.Elapsed(currentTime))
    {
        //readDht();
    }

    if (interval5Sec.Elapsed(currentTime))
    {
        //sendStatus();
    }
}

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(BAUD_RATE);
}
