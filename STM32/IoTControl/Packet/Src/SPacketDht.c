#include "SPacketDht.h"
#include "SDht.h"
#include "SIotProtocol.h"
#include "SLog.h"
#include "IoTPacketCodec.h"

void SPacketDhtRequestReceive(const uint8_t *payload, uint16_t length)
{
    (void)payload;
    (void)length;

    DhtAckData ack = {0};
    uint8_t ackPayload[DHT_ACK_DATA_SIZE];
    int16_t temperature = SDhtGetTemperature();

    ack.result = RESULT_FAIL;
    if (SDhtHasData() && SDhtGetStatus() == SDHT_STATUS_OK && temperature >= 0)
    {
        ack.result = RESULT_SUCCESS;
        ack.dht.temp = (uint16_t)(temperature / 10);
        ack.dht.humi = (uint16_t)(SDhtGetHumidity() / 10);
        SLOG_INFO("bt dht request: temp=%u, humi=%u", (unsigned int)ack.dht.temp, (unsigned int)ack.dht.humi);
    }
    else
    {
        SLOG_WARN("bt dht request: no valid reading, status=%d", (int)SDhtGetStatus());
    }

    // SIotProtocolSendPacket은 payload 바이트를 받으므로 와이어 형식(little-endian)으로 직렬화
    ackPayload[0] = ack.result;
    ackPayload[1] = (uint8_t)(ack.dht.temp & 0xFF);
    ackPayload[2] = (uint8_t)(ack.dht.temp >> 8);
    ackPayload[3] = (uint8_t)(ack.dht.humi & 0xFF);
    ackPayload[4] = (uint8_t)(ack.dht.humi >> 8);
    SIotProtocolSendPacket(ACK_DHT, ackPayload, DHT_ACK_DATA_SIZE);
}
