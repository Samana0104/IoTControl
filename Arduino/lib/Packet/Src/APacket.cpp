#include "APacket.h"
#include "APacketDht.h"

#include <AData.h>
#include <ALog.h>
#include <string.h>

// 받을 수 있는 payload 최대 크기, 넘는 프레임은 읽고 버림
static constexpr uint8_t RECEIVE_PAYLOAD_SIZE = 16;
static constexpr uint32_t LOGIN_TIMEOUT_MS = 5000;

// 서버 → 장치 패킷 처리 함수 (CRC/길이 검사를 마친 프레임, payload는 호출 동안만 유효)
typedef void (*APacketHandler)(const uint8_t *payload, uint16_t length);

typedef struct _APacketEntry
{
    uint16_t cmd;
    APacketHandler handler;
} APacketEntry;

// 수신 cmd → 처리 함수 (Raspberry5/Packet/Src/RPacket.c의 PACKET_TABLE과 같은 방식)
// 서버가 TCP 장치에 보내는 REQ는 REQ_DHT뿐
static const APacketEntry PACKET_TABLE[] = {
    {REQ_DHT, APacketDhtRequestReceive},
};

static const uint8_t PACKET_TABLE_COUNT = sizeof(PACKET_TABLE) / sizeof(PACKET_TABLE[0]);

static AWiFi *packetWiFi = nullptr;

// 프레임 수신 상태 (들어온 만큼만 모음)
static uint8_t receiveHeaderData[HEADER_SIZE];
static uint8_t receivePayload[RECEIVE_PAYLOAD_SIZE];
static HeaderData receiveHeader;
static uint8_t receiveHeaderLength = 0;
static uint16_t receivePayloadLength = 0;

static bool ReceiveFrame(HeaderData &header, const uint8_t *&payload);
static void ResetReceive();
static const APacketEntry *FindPacketEntry(uint16_t cmd);
static bool SendFrame(const uint8_t *frame, size_t frameLength);
static LoginResult SendLogin();
static LoginResult WaitLoginAck();

void APacketBegin(AWiFi &wifi, ADht &dht)
{
    packetWiFi = &wifi;
    APacketDhtBegin(dht);
}

void APacketProcess()
{
    if (packetWiFi == nullptr)
    {
        return;
    }

    HeaderData header;
    const uint8_t *payload;

    while (ReceiveFrame(header, payload))
    {
        const APacketEntry *entry = FindPacketEntry(header.cmd);
        if (entry == nullptr)
        {
            ALOG_DEBUG("server packet ignored, cmd=", header.cmd);
            continue;
        }

        entry->handler(payload, header.length);
    }
}

LoginResult APacketLogin()
{
    // 새 연결이므로 이전 연결에서 받다 만 프레임은 버림
    ResetReceive();

    LoginResult result = SendLogin();
    if (result == LOGIN_SUCCESS)
    {
        result = WaitLoginAck();
    }

    if (result == LOGIN_SUCCESS)
    {
        ALOG_INFO("Login success");
    }
    else
    {
        ALOG_WARN("Login failed, result=", (int)result);
    }
    return result;
}

bool APacketSendDhtAck(const DhtAckData &ack)
{
    uint8_t frame[HEADER_SIZE + DHT_ACK_DATA_SIZE];
    size_t frameLength = MakeDhtAckPacket(frame, sizeof(frame), &ack);

    return SendFrame(frame, frameLength);
}

// 서버 프레임을 들어온 만큼만 모음 (논블로킹)
// 프레임 하나가 다 모이고 CRC/길이가 맞으면 true, payload는 다음 호출 전까지 유효
static bool ReceiveFrame(HeaderData &header, const uint8_t *&payload)
{
    while (true)
    {
        if (receiveHeaderLength < HEADER_SIZE)
        {
            int count = packetWiFi->ReceiveFromServer(receiveHeaderData + receiveHeaderLength, HEADER_SIZE - receiveHeaderLength);
            if (count <= 0)
            {
                return false;
            }
            receiveHeaderLength += (uint8_t)count;
            if (receiveHeaderLength < HEADER_SIZE)
            {
                return false;
            }
            DecodePacketHeader(receiveHeaderData, &receiveHeader);
            receivePayloadLength = 0;
        }

        // 담을 수 없는 큰 프레임은 payload를 읽어 버림
        if (receiveHeader.length > sizeof(receivePayload))
        {
            uint8_t chunk[RECEIVE_PAYLOAD_SIZE];
            uint16_t left = receiveHeader.length - receivePayloadLength;
            int count = packetWiFi->ReceiveFromServer(chunk, left > sizeof(chunk) ? sizeof(chunk) : left);
            if (count <= 0)
            {
                return false;
            }
            receivePayloadLength += (uint16_t)count;
            if (receivePayloadLength == receiveHeader.length)
            {
                ResetReceive();
            }
            continue;
        }

        if (receivePayloadLength < receiveHeader.length)
        {
            int count = packetWiFi->ReceiveFromServer(receivePayload + receivePayloadLength, receiveHeader.length - receivePayloadLength);
            if (count <= 0)
            {
                return false;
            }
            receivePayloadLength += (uint16_t)count;
            if (receivePayloadLength < receiveHeader.length)
            {
                return false;
            }
        }

        bool valid = CheckPacketCrc(receiveHeaderData, &receiveHeader, receivePayload) == 0 &&
                     CheckPacketLength(receiveHeader.cmd, receiveHeader.length) == 0;
        header = receiveHeader;
        payload = receivePayload;
        // 다음 호출은 새 헤더부터 받음 (payload 버퍼는 그때까지 유지)
        receiveHeaderLength = 0;
        if (valid)
        {
            return true;
        }

        // CRC가 틀리면 프레임 경계를 믿을 수 없으므로 연결을 다시 맺게 함
        ALOG_WARN("bad frame from server, disconnect");
        packetWiFi->DisconnectServer();
        ResetReceive();
        return false;
    }
}

static void ResetReceive()
{
    receiveHeaderLength = 0;
    receivePayloadLength = 0;
}

static const APacketEntry *FindPacketEntry(uint16_t cmd)
{
    for (uint8_t i = 0; i < PACKET_TABLE_COUNT; ++i)
    {
        if (PACKET_TABLE[i].cmd == cmd)
        {
            return &PACKET_TABLE[i];
        }
    }
    return nullptr;
}

// Make*Packet 결과(0이면 실패)를 전부 보냈으면 true
static bool SendFrame(const uint8_t *frame, size_t frameLength)
{
    return packetWiFi != nullptr && frameLength > 0 && packetWiFi->SendToServer(frame, frameLength) == frameLength;
}

// 저장된 계정으로 REQ_LOGIN 전송, 보냈으면 LOGIN_SUCCESS
static LoginResult SendLogin()
{
    MemData member;
    if (!LoadData(DATA_ADDR_MEMBER, reinterpret_cast<uint8_t *>(&member), sizeof(member)))
    {
        return LOGIN_NO_MEMBER;
    }

    uint8_t frame[HEADER_SIZE + MEM_DATA_SIZE];
    size_t frameLength = MakeLoginPacket(frame, sizeof(frame), &member);
    bool sent = SendFrame(frame, frameLength);

    // pw가 스택에 남지 않게 지움
    memset(&member, 0, sizeof(member));
    memset(frame, 0, sizeof(frame));

    return sent ? LOGIN_SUCCESS : LOGIN_NO_SERVER;
}

// 수신은 ReceiveFrame 한 곳에서만 (프레임 경계가 어긋나지 않게)
static LoginResult WaitLoginAck()
{
    uint32_t startMs = millis();
    while (millis() - startMs < LOGIN_TIMEOUT_MS)
    {
        HeaderData header;
        const uint8_t *payload;

        if (!ReceiveFrame(header, payload) || header.cmd != ACK_LOGIN)
        {
            continue;
        }

        ResultData result;
        if (ReadResultData(payload, header.length, &result) != 0)
        {
            return LOGIN_BAD_PACKET;
        }
        return result.result == RESULT_SUCCESS ? LOGIN_SUCCESS : LOGIN_REJECTED;
    }
    return LOGIN_TIMEOUT;
}
