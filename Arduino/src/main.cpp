#include <Arduino.h>
#include <ADefine.h>
#include <AData.h>
#include <ADht.h>
#include <AWiFi.h>
#include <AIntervalMS.h>
#include <ALog.h>
#include <APacket.h>

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

static constexpr uint32_t INTERVAL_MS_POLL = 200;
static constexpr uint32_t INTERVAL_MS_5Sec = 5000;

static IntervalMS intervalPoll(INTERVAL_MS_POLL);
static IntervalMS interval5Sec(INTERVAL_MS_5Sec);

void loop()
{
    // 내부에서만 사용

#ifdef DEBUG_CLI
    CLIHandler.ReadSerial();
#endif

    uint32_t currentTime = millis();

    // 수신 확인(available)은 데이터가 없으면 ESP에 AT 커맨드를 보내므로 주기적으로만
    if (!tryServerConnect && intervalPoll.Elapsed(currentTime))
    {
        APacketProcess();
    }

    if (interval5Sec.Elapsed(currentTime))
    {
        // 서버가 끊었거나 잘못된 프레임으로 끊은 뒤 다시 접속·로그인
        if (!tryServerConnect && !WiFiModule.IsServerConnected())
        {
            ALOG_WARN("Raspberry Server disconnected");
            tryServerConnect = true;
        }

        if (WiFiModule.IsConnected() && tryServerConnect)
        {
            ALOG_INFO("Raspberry Server try Connect");
            if (WiFiModule.ConnectServer("10.10.16.76", 5000))
            {
                ALOG_INFO("Raspberry Server Connected");
                tryServerConnect = false;

                // 저장된 계정(member set)으로 로그인 요청
                APacketLogin();
            }
        }

        // 5초마다 실행할 코드 작성
    }
}

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(BAUD_RATE);
    Dht.Begin();
    BeginLog(Serial);
    ALOG_INFO("Boot Arduino");

    APacketBegin(WiFiModule, Dht);

    if (!WiFiModule.Begin(ESP_BAUD_RATE))
    {
        ALOG_WARN("wifi module not found, baud=", ESP_BAUD_RATE);
    }

#ifdef DEBUG_CLI
    BindWiFi(WiFiModule);
    ALOG_INFO("Debug CLI initialized");
#endif
}
