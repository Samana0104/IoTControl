#include <Arduino.h>
#include <ADefine.h>
#include <ADht.h>
#include <AWiFi.h>
#include <AIntervalMS.h>
#include <ALog.h>

#ifdef DEBUG_CLI
#include <ACLI.h>
#include <ACmdWiFi.h>
#endif

// ESP8266 연결용 (SoftwareSerial)
const int RX_PIN = 10;
const int TX_PIN = 11;
const long BAUD_RATE = 115200;
const long ESP_BAUD_RATE = 38400;
const int DHT_PIN = A0;
static char buf[20];

static AWiFi WiFiModule(RX_PIN, TX_PIN);
static ADht Dht(DHT_PIN);
static bool tryServerConnect = true;

#ifdef DEBUG_CLI
static ACLI CLIHandler(Serial);
#endif



void loop()
{
    // 내부에서만 사용
    static constexpr uint32_t INTERVAL_MS_2Sec = 2000;

    static IntervalMS interval2Sec(INTERVAL_MS_2Sec);



#ifdef DEBUG_CLI
    CLIHandler.ReadSerial();
#endif
    if(tryServerConnect)
    {
        ALOG_INFO("Raspberry Server try Connect");
        if(WiFiModule.ConnectServer("10.10.16.76",5000))
        {
            ALOG_INFO("Raspberry Server Connected");
            
        }
        tryServerConnect = false;
    }

    uint32_t currentTime = millis();

    if (interval2Sec.Elapsed(currentTime))
    {
        
        uint8_t humidity = Dht.GetHumidity();
        uint8_t temperature = Dht.GetTemperature();
        ALOG_INFO("humi %ul, temp %ul",humidity, temperature);
        if(WiFiModule.IsServerConnected())
        {
            WiFiModule.dhtSendToServer(&humidity,&temperature);
        }
    }

}

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(BAUD_RATE);
    Dht.Begin();
    BeginLog(Serial);
    ALOG_INFO("Boot Arduino");

    if (!WiFiModule.Begin(ESP_BAUD_RATE))
    {
        ALOG_WARN("wifi module not found, baud=", ESP_BAUD_RATE);
    }

#ifdef DEBUG_CLI
    BindWiFi(WiFiModule);
    ALOG_INFO("Debug CLI initialized");
#endif
}
