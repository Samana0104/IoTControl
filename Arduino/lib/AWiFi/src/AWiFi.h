#pragma once

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <WiFiEspAT.h>
#include <ADefine.h>

typedef enum
{
    LOGIN_SUCCESS = 0,
    LOGIN_REJECTED,    // 서버가 RESULT_FAIL 응답 (ID/PW 틀림)
    LOGIN_NO_SERVER,   // 서버 미연결 또는 전송 실패
    LOGIN_TIMEOUT,     // ACK_LOGIN 응답 없음
    LOGIN_BAD_PACKET   // CRC/길이가 맞지 않는 응답
} LoginResult;

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

    // One plain TCP connection. host is an IP address or hostname, not a URL.
    // ConnectServer replaces the previous socket and may wait for AT commands.
    bool ConnectServer(const char *host, uint16_t port);
    void DisconnectServer();
    bool IsServerConnected();

    // Returns bytes accepted by the client, not an acknowledgment from the server.
    size_t SendToServer(const uint8_t *data, size_t length);
    size_t SendToServer(const char *text);
    size_t dhtSendToServer(uint8_t* humi, uint8_t* temp);

    int ServerAvailable();
    // Reads up to length currently available bytes. Does not append '\0'.
    // Returns 0 for no data/invalid arguments. AT command handling may still wait.
    int ReceiveFromServer(uint8_t *buffer, size_t length);

    // REQ_LOGIN 전송 후 ACK_LOGIN을 timeoutMs까지 기다림 (블로킹)
    // 그 사이 들어온 다른 프레임은 버림
    LoginResult LoginToServer(const MemData &member, uint32_t timeoutMs = LOGIN_TIMEOUT_MS);

    // 서버 프레임을 들어온 만큼만 모음 (논블로킹, loop에서 계속 호출)
    // 프레임 하나가 다 모이고 CRC가 맞으면 true, payload는 다음 호출 전까지 유효
    // payload가 RECEIVE_PAYLOAD_SIZE보다 큰 프레임은 읽고 버림
    bool PollServerPacket(HeaderData &header, const uint8_t *&payload);

    // 서버 REQ_DHT에 대한 ACK_DHT 응답
    bool SendDhtAck(const DhtAckData &ack);

    // 전원 인가 시 저장된 AP로 자동 접속 여부 (ESP 플래시에 저장)
    bool SetAutoConnect(bool enable);

    void PrintStatus(Print &out);
    void PrintScan(Print &out);

    

  private:
    // startMs부터 timeoutMs 안에 정확히 length 바이트를 읽음
    bool ReceiveExact(uint8_t *buffer, size_t length, uint32_t startMs, uint32_t timeoutMs);
    void ResetPoll();

  private:
    static constexpr uint8_t SCAN_MAX = 5;
    static constexpr uint32_t LOGIN_TIMEOUT_MS = 5000;
    // ACK 대기 중 받을 수 있는 payload 최대 크기, 넘는 프레임은 읽고 버림
    static constexpr uint8_t RECEIVE_PAYLOAD_SIZE = 16;

    SoftwareSerial serial;
    WiFiClient client;

    long baudRate = 0;
    bool ready = false;

    // PollServerPacket 수신 상태
    uint8_t pollHeaderData[HEADER_SIZE];
    uint8_t pollPayload[RECEIVE_PAYLOAD_SIZE];
    HeaderData pollHeader = {};
    uint8_t pollHeaderLength = 0;
    uint16_t pollPayloadLength = 0;
};
