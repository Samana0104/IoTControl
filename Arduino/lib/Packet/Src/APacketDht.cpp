#include "APacketDht.h"
#include "APacket.h"

#include <ALog.h>

static ADht *packetDht = nullptr;

void APacketDhtBegin(ADht &dht)
{
    packetDht = &dht;
}

void APacketDhtRequestReceive(const uint8_t *payload, uint16_t length)
{
    (void)payload;
    (void)length;

    uint8_t temperature = 0;
    uint8_t humidity = 0;
    bool valid = packetDht->Read(temperature, humidity);

    DhtAckData ack = {};
    ack.result = valid ? RESULT_SUCCESS : RESULT_FAIL;
    if (valid)
    {
        ack.dht.temp = temperature;
        ack.dht.humi = humidity;
    }

    if (!APacketSendDhtAck(ack))
    {
        ALOG_WARN("ACK_DHT send failed");
        return;
    }
    ALOG_INFO("ACK_DHT sent, result=", ack.result, " temp=", ack.dht.temp, " humi=", ack.dht.humi);
}
