#include <Arduino.h>
#include <ADefine.h>
#include <DHT.h>
#include <AWiFi.h>
#include <AIntervalMS.h>

#ifdef DEBUG_CLI
#include <ACLI.h>
#include <ACommand.h>
#endif

// ESP8266 연결용 (SoftwareSerial)
const int RX_PIN = 10;
const int TX_PIN = 11;
const long BAUD_RATE = 115200;
const long ESP_BAUD_RATE = 38400;

static AWiFi WiFiModule(RX_PIN, TX_PIN);

#ifdef DEBUG_CLI
static ACLI CLIHandler(Serial);
#endif

void loop()
{
    // 내부에서만 사용
    static constexpr uint32_t INTERVAL_MS_2Sec = 2000;
    static constexpr uint32_t INTERVAL_MS_5Sec = 5000;

    static IntervalMS interval2Sec(INTERVAL_MS_2Sec);
    static IntervalMS interval5Sec(INTERVAL_MS_5Sec);

#ifdef DEBUG_CLI
    CLIHandler.ReadSerial();
#endif

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

    WiFiModule.Begin(ESP_BAUD_RATE);

#ifdef DEBUG_CLI
    BindWiFi(WiFiModule);
#endif
}
