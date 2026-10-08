#include "PPacketMonitor.h"
#include "IoTPacketCodec.h"
#include "PServerConnection.h"
#include <QDateTime>
#include <algorithm>

namespace
{
QString CommandName(uint16_t command)
{
    switch (command)
    {
    case REQ_LOGIN:
        return QStringLiteral("REQ_LOGIN");
    case ACK_LOGIN:
        return QStringLiteral("ACK_LOGIN");
    case NFY_CHAT:
        return QStringLiteral("NFY_CHAT");
    case NFY_DHT:
        return QStringLiteral("NFY_DHT");
    case NFY_FAN:
        return QStringLiteral("NFY_FAN");
    case NFY_CON:
        return QStringLiteral("NFY_CON");
    case REQ_DHT:
        return QStringLiteral("REQ_DHT");
    case ACK_DHT:
        return QStringLiteral("ACK_DHT");
    case REQ_DHT_ALL:
        return QStringLiteral("REQ_DHT_ALL");
    case NFY_DHT_ROW:
        return QStringLiteral("NFY_DHT_ROW");
    case ACK_DHT_ALL:
        return QStringLiteral("ACK_DHT_ALL");
    case REQ_DHT_COLLECT:
        return QStringLiteral("REQ_DHT_COLLECT");
    case ACK_DHT_COLLECT:
        return QStringLiteral("ACK_DHT_COLLECT");
    case REQ_DHT_REFRESH:
        return QStringLiteral("REQ_DHT_REFRESH");
    case ACK_DHT_REFRESH:
        return QStringLiteral("ACK_DHT_REFRESH");
    case REQ_FAN:
        return QStringLiteral("REQ_FAN");
    case ACK_FAN:
        return QStringLiteral("ACK_FAN");
    case REQ_FAN_QUERY:
        return QStringLiteral("REQ_FAN_QUERY");
    case ACK_FAN_QUERY:
        return QStringLiteral("ACK_FAN_QUERY");
    case REQ_FAN_UPDATE:
        return QStringLiteral("REQ_FAN_UPDATE");
    case ACK_FAN_UPDATE:
        return QStringLiteral("ACK_FAN_UPDATE");
    case REQ_FAN_APPLY:
        return QStringLiteral("REQ_FAN_APPLY");
    case ACK_FAN_APPLY:
        return QStringLiteral("ACK_FAN_APPLY");
    case REQ_SESSION_ALL:
        return QStringLiteral("REQ_SESSION_ALL");
    case NFY_SESSION_ROW:
        return QStringLiteral("NFY_SESSION_ROW");
    case ACK_SESSION_ALL:
        return QStringLiteral("ACK_SESSION_ALL");
    case REQ_BT_CONNECT:
        return QStringLiteral("REQ_BT_CONNECT");
    case ACK_BT_CONNECT:
        return QStringLiteral("ACK_BT_CONNECT");
    case REQ_BT_REGISTER:
        return QStringLiteral("REQ_BT_REGISTER");
    case ACK_BT_REGISTER:
        return QStringLiteral("ACK_BT_REGISTER");
    case REQ_BT_CONNECT_ALL:
        return QStringLiteral("REQ_BT_CONNECT_ALL");
    case NFY_BT_CONNECT_ROW:
        return QStringLiteral("NFY_BT_CONNECT_ROW");
    case ACK_BT_CONNECT_ALL:
        return QStringLiteral("ACK_BT_CONNECT_ALL");
    case REQ_BT_SCAN:
        return QStringLiteral("REQ_BT_SCAN");
    case NFY_BT_SCAN_ROW:
        return QStringLiteral("NFY_BT_SCAN_ROW");
    case ACK_BT_SCAN:
        return QStringLiteral("ACK_BT_SCAN");
    case REQ_FW_BEGIN:
        return QStringLiteral("REQ_FW_BEGIN");
    case ACK_FW_BEGIN:
        return QStringLiteral("ACK_FW_BEGIN");
    case REQ_FW_CHUNK:
        return QStringLiteral("REQ_FW_CHUNK");
    case ACK_FW_CHUNK:
        return QStringLiteral("ACK_FW_CHUNK");
    case REQ_FW_END:
        return QStringLiteral("REQ_FW_END");
    case ACK_FW_END:
        return QStringLiteral("ACK_FW_END");
    default:
        return QStringLiteral("UNKNOWN");
    }
}
QString ReadText(const char *data, size_t size)
{
    const auto *end = std::find(data, data + size, '\0');
    QString text = QString::fromUtf8(data, end - data);
    for (QChar &character : text)
        if (character.unicode() < 32 || character.unicode() == 127)
            character = QLatin1Char(' ');
    return text;
}
} // namespace
PacketMonitor::PacketMonitor(ServerConnection *connection, QObject *parent) : QObject(parent)
{
    connect(connection, &ServerConnection::DataReceived, this, [this](const QByteArray &data)
            { Observe(data, false); });
    connect(connection, &ServerConnection::DataSent, this, [this](const QByteArray &data)
            { Observe(data, true); });
    connect(connection, &ServerConnection::Connected, this, &PacketMonitor::Reset);
    connect(connection, &ServerConnection::Disconnected, this, &PacketMonitor::Reset);
    connect(connection, &ServerConnection::ConnectionFailed, this, &PacketMonitor::Reset);
}
void PacketMonitor::Reset()
{
    receiveBuffer.fill('\0');
    receiveBuffer.clear();
    sendBuffer.fill('\0');
    sendBuffer.clear();
    receiveDiscard = sendDiscard = 0;
}
void PacketMonitor::Observe(const QByteArray &data, bool sent)
{
    QByteArray &buffer = sent ? sendBuffer : receiveBuffer;
    qsizetype &discard = sent ? sendDiscard : receiveDiscard;
    qsizetype offset = 0;
    while (offset < data.size())
    {
        if (discard > 0)
        {
            const qsizetype COUNT = std::min(discard, data.size() - offset);
            discard -= COUNT;
            offset += COUNT;
            continue;
        }
        if (buffer.size() < HEADER_SIZE)
        {
            const qsizetype COUNT = std::min(HEADER_SIZE - buffer.size(), data.size() - offset);
            buffer.append(data.constData() + offset, COUNT);
            offset += COUNT;
            if (buffer.size() < HEADER_SIZE)
                return;
        }
        HeaderData header{};
        DecodePacketHeader(reinterpret_cast<const uint8_t *>(buffer.constData()), &header);
        const QString PREFIX = QStringLiteral("[%1] %2  %3 (0x%4)  len=%5").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), sent ? QStringLiteral("TX") : QStringLiteral("RX"), CommandName(header.cmd), QString::number(header.cmd, 16).rightJustified(4, QLatin1Char('0')).toUpper()).arg(header.length);
        if (header.length > MAX_PAYLOAD_SIZE)
        {
            emit LineReady(PREFIX + tr("  | 표시 한도 초과 · payload 생략"), sent, false);
            buffer.fill('\0');
            buffer.clear();
            discard = header.length;
            continue;
        }
        const qsizetype TOTAL = HEADER_SIZE + header.length;
        const qsizetype COUNT = std::min(TOTAL - buffer.size(), data.size() - offset);
        buffer.append(data.constData() + offset, COUNT);
        offset += COUNT;
        if (buffer.size() < TOTAL)
            return;
        const auto *frame = reinterpret_cast<const uint8_t *>(buffer.constData());
        const bool CRC_VALID = CheckPacketCrc(frame, &header, frame + HEADER_SIZE) == 0;
        const bool LENGTH_VALID = CheckPacketLength(header.cmd, header.length) == 0;
        const QString DETAIL = !CRC_VALID ? tr("CRC 오류") : !LENGTH_VALID ? tr("미지원 명령 또는 payload 길이 오류")
                                                                           : Describe(header.cmd, frame + HEADER_SIZE, header.length);
        emit LineReady(PREFIX + QStringLiteral("  | ") + DETAIL, sent, CRC_VALID && LENGTH_VALID);
        buffer.fill('\0');
        buffer.clear();
    }
}
QString PacketMonitor::Describe(uint16_t command, const uint8_t *payload, size_t length) const
{
    if (command == REQ_LOGIN || command == REQ_BT_CONNECT)
        return tr("id=%1 · 비밀번호 숨김").arg(ReadText(reinterpret_cast<const char *>(payload), MEM_ID_SIZE));
    if (command == REQ_BT_REGISTER)
        return tr("MAC 등록 요청 · PIN 숨김");
    if (length == 0)
        return tr("요청 · payload 없음");
    if (command == NFY_DHT_ROW)
    {
        DhtRowData row{};
        ReadDhtRowData(payload, length, &row);
        return tr("id=%1 · 온도=%2°C · 습도=%3% · 시각=%4").arg(ReadText(row.id, MEM_ID_SIZE)).arg(row.temp).arg(row.humi).arg(ReadText(row.updatedAt, DHT_TIMESTAMP_SIZE));
    }
    if (command == NFY_DHT || command == ACK_DHT)
    {
        DhtData data{};
        if (command == ACK_DHT)
        {
            DhtAckData ack{};
            ReadDhtAckData(payload, length, &ack);
            if (ack.result != RESULT_SUCCESS)
                return tr("측정 실패");
            data = ack.dht;
        }
        else
            ReadDhtData(payload, length, &data);
        return tr("온도=%1°C · 습도=%2%").arg(data.temp).arg(data.humi);
    }
    if (command == REQ_FAN || command == NFY_FAN || command == REQ_FAN_UPDATE)
    {
        FanData data{};
        ReadFanData(payload, length, &data);
        return tr("팬 속도=%1%").arg(data.fanSpeed);
    }
    if (command == ACK_FAN_QUERY)
    {
        FanQueryAckData ack{};
        ReadFanQueryAckData(payload, length, &ack);
        return ack.result == RESULT_SUCCESS ? tr("DB 팬 속도=%1%").arg(ack.fan.fanSpeed) : tr("팬 조회 실패");
    }
    if (command == REQ_FAN_APPLY)
    {
        FanApplyData data{};
        ReadFanApplyData(payload, length, &data);
        return tr("id=%1 · 팬 적용=%2%").arg(ReadText(data.id, MEM_ID_SIZE)).arg(data.fan.fanSpeed);
    }
    if (command == ACK_FAN_APPLY)
    {
        FanApplyAckData ack{};
        ReadFanApplyAckData(payload, length, &ack);
        return tr("result=%1 · reason=%2 · 팬=%3%").arg(ack.result).arg(ack.reason).arg(ack.fan.fanSpeed);
    }
    if (command == REQ_DHT_REFRESH)
        return tr("개별 측정 요청 · id=%1").arg(ReadText(reinterpret_cast<const char *>(payload), MEM_ID_SIZE));
    if (command == NFY_SESSION_ROW)
    {
        SessionRowData row{};
        ReadSessionRowData(payload, length, &row);
        return tr("id=%1 · 장치=%2 · 연결=%3").arg(ReadText(row.id, MEM_ID_SIZE), row.memberType == 1 ? QStringLiteral("STM32") : row.memberType == 2 ? QStringLiteral("ARDUINO")
                                                                                                                                                      : tr("알 수 없음"),
                                                   row.links == 3 ? QStringLiteral("TCP + Bluetooth") : row.links == 2 ? QStringLiteral("Bluetooth")
                                                                                                    : row.links == 1   ? QStringLiteral("TCP")
                                                                                                                       : tr("없음"));
    }
    if (command == NFY_BT_SCAN_ROW)
    {
        BtScanRowData row{};
        ReadBtScanRowData(payload, length, &row);
        return tr("MAC=%1 · 이름=%2 · RSSI=%3dBm · paired=%4").arg(ReadText(row.mac, BLUETOOTH_MAC_SIZE), ReadText(row.name, BT_SCAN_NAME_SIZE)).arg(row.rssi).arg(row.paired);
    }
    if (command == NFY_BT_CONNECT_ROW)
    {
        BtConnectRowData row{};
        ReadBtConnectRowData(payload, length, &row);
        return tr("id=%1 · %2").arg(ReadText(row.id, MEM_ID_SIZE), row.status == 0 ? tr("연결 성공") : row.status == 1 ? tr("이미 연결됨")
                                                                                                   : row.status == 2   ? tr("미등록 장치")
                                                                                                                       : tr("연결 실패"));
    }
    if (command == ACK_FW_CHUNK)
    {
        FirmwareChunkAckData ack{};
        ReadFirmwareChunkAckData(payload, length, &ack);
        return tr("펌웨어 청크 %1 · offset=%2").arg(ack.result == RESULT_SUCCESS ? tr("성공") : tr("실패")).arg(ack.offset);
    }
    if (IS_ACK(command))
        return tr("%1%2").arg(payload[0] == RESULT_SUCCESS ? tr("성공") : tr("실패"), length >= 2 ? tr(" · reason=%1").arg(payload[1]) : QString());
    if (command == NFY_CHAT)
        return tr("메시지=%1").arg(ReadText(reinterpret_cast<const char *>(payload), length));
    if (command == NFY_CON)
    {
        ConData data{};
        ReadConData(payload, length, &data);
        return tr("설정 온도=%1").arg(data.tempData);
    }
    return tr("payload %1바이트 · 세부 해석 미지원").arg(length);
}
