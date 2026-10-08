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

    float humidity = packetDht->GetHumidity();
    float temperature = packetDht->GetTemperature();
    bool valid = !isnan(humidity) && !isnan(temperature) && temperature >= 0;

    DhtAckData ack = {};
    ack.result = valid ? RESULT_SUCCESS : RESULT_FAIL;
    if (valid)
    {
        ack.dht.temp = (uint16_t)temperature;
        ack.dht.humi = (uint16_t)humidity;
    }

    if (!APacketSendDhtAck(ack))
    {
        ALOG_WARN("ACK_DHT send failed");
        return;
    }
    ALOG_INFO("ACK_DHT sent, result=", ack.result, " temp=", ack.dht.temp, " humi=", ack.dht.humi);
}
