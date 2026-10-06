#include "AWiFi.h"

#include <WiFiEspAT.h>

bool AWiFi::Begin(long _baudRate)
{
    baudRate = _baudRate;
    serial.begin(baudRate);

    ready = WiFi.init(serial);
    return ready;
}

// 모듈 UART 설정을 바꾸고(플래시에 저장) 새 보레이트로 다시 초기화
// 현재 보레이트로 TX만 되면 되므로 수신이 깨지는 115200에서도 동작함
bool AWiFi::SetModuleBaud(long newBaudRate)
{
    serial.print(F("AT+UART_DEF="));
    serial.print(newBaudRate);
    serial.print(F(",8,1,0,0\r\n"));
    serial.flush();
    delay(100);

    return Begin(newBaudRate);
}

bool AWiFi::Connect(const char *ssid, const char *pass)
{
    if (!ready)
    {
        return false;
    }

    // 이번 접속만 플래시에 저장 -> 재부팅 후에도 ESP가 자동 접속
    WiFi.setPersistent(true);
    int status = WiFi.begin(ssid, pass[0] != '\0' ? pass : nullptr);
    WiFi.setPersistent(false);

    return status == WL_CONNECTED;
}

void AWiFi::Disconnect()
{
    if (ready)
    {
        // 저장된 AP도 지우고 자동 접속 해제
        WiFi.disconnect(true);
    }
}

bool AWiFi::IsConnected()
{
    return ready && WiFi.status() == WL_CONNECTED;
}

bool AWiFi::SetAutoConnect(bool enable)
{
    return ready && WiFi.setAutoConnect(enable);
}

void AWiFi::PrintStatus(Print &out)
{
    out.print(F("module:   "));
    out.println(ready ? F("OK") : F("not found"));
    out.print(F("baud:     "));
    out.println(baudRate);

    if (!ready)
    {
        return;
    }

    char buffer[33];

    out.print(F("firmware: "));
    out.println(WiFi.firmwareVersion(buffer));

    if (WiFi.status() != WL_CONNECTED)
    {
        out.println(F("status:   disconnected"));
        return;
    }

    out.println(F("status:   connected"));
    out.print(F("ssid:     "));
    out.println(WiFi.SSID(buffer));
    out.print(F("ip:       "));
    out.println(WiFi.localIP());
    out.print(F("rssi:     "));
    out.print(WiFi.RSSI());
    out.println(F(" dBm"));
}

void AWiFi::PrintScan(Print &out)
{
    if (!ready)
    {
        out.println(F("module not found"));
        return;
    }

    // 내부 배열을 쓰는 scanNetworks()는 RAM을 많이 잡아서 스택 배열 사용
    WiFiApData aps[SCAN_MAX];
    int8_t count = WiFi.scanNetworks(aps, SCAN_MAX);
    if (count <= 0)
    {
        out.println(F("no networks"));
        return;
    }

    out.println(F("rssi\tenc\tssid"));
    for (int8_t i = 0; i < count; ++i)
    {
        out.print(aps[i].rssi);
        out.print('\t');
        out.print(aps[i].enc == 0 ? F("open") : F("lock"));
        out.print('\t');
        out.println(aps[i].ssid);
    }
}
