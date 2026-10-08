#include <Arduino.h>
#include <ADefine.h>
#include <AData.h>
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

// 마지막으로 성공한 DHT 값 (REQ_DHT 응답용)
static bool dhtValid = false;
static uint16_t lastTemperature = 0;
static uint16_t lastHumidity = 0;

#ifdef DEBUG_CLI
static ACLI CLIHandler(Serial);
#endif

static constexpr uint32_t INTERVAL_MS_2Sec = 2000;
static constexpr uint32_t INTERVAL_MS_5Sec = 5000;

static IntervalMS interval2Sec(INTERVAL_MS_2Sec);
static IntervalMS interval5Sec(INTERVAL_MS_5Sec);


// DHT11 값을 읽어 저장, 실패(NaN)면 다음 REQ_DHT에 RESULT_FAIL로 응답
static void ReadDht()
{
    float humidity = Dht.GetHumidity();
    float temperature = Dht.GetTemperature();

    dhtValid = !isnan(humidity) && !isnan(temperature) && temperature >= 0;
    if (dhtValid)
    {
        lastTemperature = (uint16_t)temperature;
        lastHumidity = (uint16_t)humidity;
    }
}

// 서버 → 장치 REQ_DHT: 마지막 측정값을 ACK_DHT로 응답
static void HandleDhtRequest()
{
    DhtAckData ack = {};

    ack.result = dhtValid ? RESULT_SUCCESS : RESULT_FAIL;
    if (dhtValid)
    {
        ack.dht.temp = lastTemperature;
        ack.dht.humi = lastHumidity;
    }
    if (!WiFiModule.SendDhtAck(ack))
    {
        ALOG_WARN("ACK_DHT send failed");
        return;
    }
    ALOG_INFO("ACK_DHT sent, result=", ack.result, " temp=", ack.dht.temp, " humi=", ack.dht.humi);
}

static void HandleServerPackets()
{
    HeaderData header;
    const uint8_t *payload;

    while (WiFiModule.PollServerPacket(header, payload))
    {
        if (header.cmd == REQ_DHT)
        {
            HandleDhtRequest();
        }
        else
        {
            ALOG_DEBUG("server packet ignored, cmd=", header.cmd);
        }
    }
}

void loop()
{
    // 내부에서만 사용

#ifdef DEBUG_CLI
    CLIHandler.ReadSerial();
#endif

    uint32_t currentTime = millis();

    if (interval2Sec.Elapsed(currentTime))
    {
        ReadDht();
    }

    if (!tryServerConnect)
    {
        HandleServerPackets();
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
                MemData member;
                if (LoadData(DATA_ADDR_MEMBER, reinterpret_cast<uint8_t *>(&member), sizeof(member)))
                {
                    LoginResult result = WiFiModule.LoginToServer(member);
                    memset(&member, 0, sizeof(member));

                    if (result == LOGIN_SUCCESS)
                    {
                        ALOG_INFO("Login success");
                    }
                    else
                    {
                        ALOG_WARN("Login failed, result=", (int)result);
                    }
                }
                else
                {
                    ALOG_WARN("No member saved, skip login");
                }
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

    if (!WiFiModule.Begin(ESP_BAUD_RATE))
    {
        ALOG_WARN("wifi module not found, baud=", ESP_BAUD_RATE);
    }

#ifdef DEBUG_CLI
    BindWiFi(WiFiModule);
    ALOG_INFO("Debug CLI initialized");
#endif
}
