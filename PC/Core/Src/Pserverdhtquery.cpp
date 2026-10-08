#include "Pserverdhtquery.h"

#include "IoTPacketCodec.h"
#include "Pserverconnection.h"

#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

ServerDhtQuery::ServerDhtQuery(ServerConnection *connection, QObject *parent) : QObject(parent), connection(connection), queryTimer(new QTimer(this))
{
    queryTimer->setObjectName(QStringLiteral("dhtQueryTimer"));
    queryTimer->setSingleShot(true);
    connect(queryTimer, &QTimer::timeout, this, &ServerDhtQuery::HandleTimeout);
    connect(connection, &ServerConnection::DataReceived, this, &ServerDhtQuery::ReceiveData);
    connect(connection, &ServerConnection::Disconnected, this, &ServerDhtQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::ConnectionFailed, this, &ServerDhtQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::Connected, this, &ServerDhtQuery::CancelQuery);
}

void ServerDhtQuery::LoadAllDht(int timeoutMs)
{
    if (loading)
        return;
    if (!connection->IsConnected())
    {
        emit QueryFailed(tr("dht 조회 전에 서버에 접속해 주세요."));
        return;
    }
    uint8_t frame[HEADER_SIZE]{};
    const size_t LENGTH = MakeDhtAllPacket(frame, sizeof(frame));
    pendingRecords.clear();
    loading = true;
    queryTimer->start(std::max(1, timeoutMs));
    const QByteArray PACKET(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(LENGTH));
    if (LENGTH == 0 || !connection->SendPacket(PACKET))
        FailQuery(tr("dht 전체 조회 요청을 전송하지 못했습니다."), false);
}

void ServerDhtQuery::CancelQuery()
{
    PauseQuery();
    receiveBuffer.clear();
    discardRemaining = 0;
}

void ServerDhtQuery::PauseQuery()
{
    // 같은 TCP 연결에서 로그인 화면으로 돌아가도 수신 프레임 경계는 유지합니다.
    loading = false;
    queryTimer->stop();
    pendingRecords.clear();
}

bool ServerDhtQuery::IsLoading() const { return loading; }

void ServerDhtQuery::ReceiveData(const QByteArray &data)
{
    qsizetype offset = 0;
    while (connection->IsConnected() && offset < data.size())
    {
        if (discardRemaining > 0)
        {
            const qsizetype COUNT = std::min(discardRemaining, data.size() - offset);
            discardRemaining -= COUNT;
            offset += COUNT;
            continue;
        }
        if (receiveBuffer.size() < HEADER_SIZE)
        {
            const qsizetype COUNT = std::min(HEADER_SIZE - receiveBuffer.size(), data.size() - offset);
            receiveBuffer.append(data.constData() + offset, COUNT);
            offset += COUNT;
            if (receiveBuffer.size() < HEADER_SIZE)
                return;
        }
        HeaderData header{};
        DecodePacketHeader(reinterpret_cast<const uint8_t *>(receiveBuffer.constData()), &header);
        if (loading && (header.cmd == NFY_DHT_ROW || header.cmd == ACK_DHT_ALL) && CheckPacketLength(header.cmd, header.length) != 0)
        {
            FailQuery(tr("dht 조회 응답 길이가 올바르지 않습니다."), true);
            return;
        }
        if (header.length > MAX_PAYLOAD_SIZE)
        {
            receiveBuffer.clear();
            discardRemaining = header.length;
            continue;
        }
        const qsizetype FRAME_LENGTH = HEADER_SIZE + header.length;
        const qsizetype COUNT = std::min(FRAME_LENGTH - receiveBuffer.size(), data.size() - offset);
        receiveBuffer.append(data.constData() + offset, COUNT);
        offset += COUNT;
        if (receiveBuffer.size() < FRAME_LENGTH)
            return;
        const auto *frame = reinterpret_cast<const uint8_t *>(receiveBuffer.constData());
        if (loading && CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0)
        {
            FailQuery(tr("dht 조회 응답 CRC가 올바르지 않습니다."), true);
            return;
        }
        if (loading && header.cmd == NFY_DHT_ROW && !StoreDhtRow(frame + HEADER_SIZE, header.length))
        {
            FailQuery(tr("dht 조회 결과의 데이터 형식이 올바르지 않습니다."), true);
            return;
        }
        if (loading && header.cmd == ACK_DHT_ALL)
        {
            ResultData result{};
            if (ReadResultData(frame + HEADER_SIZE, header.length, &result) != 0 || (result.result != RESULT_SUCCESS && result.result != RESULT_FAIL))
            {
                FailQuery(tr("dht 조회 완료 응답이 올바르지 않습니다."), true);
                return;
            }
            if (result.result == RESULT_FAIL)
                FailQuery(tr("서버의 dht SELECT가 실패했습니다. 서버 DB 설정과 테이블 구조를 확인해 주세요."), false);
            else
            {
                const DhtRecords RECORDS = pendingRecords.values();
                loading = false;
                queryTimer->stop();
                pendingRecords.clear();
                emit DhtLoaded(RECORDS);
            }
        }
        receiveBuffer.clear();
    }
}

bool ServerDhtQuery::StoreDhtRow(const uint8_t *payload, size_t length)
{
    DhtRowData data{};
    if (ReadDhtRowData(payload, length, &data) != 0 || !std::isfinite(data.temp) || !std::isfinite(data.humi) || data.memberType > DHT_MEMBER_PC)
        return false;
    const QByteArray ID(data.id, static_cast<qsizetype>(std::find(data.id, data.id + MEM_ID_SIZE, '\0') - data.id));
    const QString MEMBER_ID = QString::fromUtf8(ID);
    if (ID.isEmpty() || MEMBER_ID.toUtf8() != ID)
        return false;
    DhtRecord record;
    record.id = MEMBER_ID;
    record.temp = data.temp;
    record.humi = data.humi;
    const QByteArray TIMESTAMP(data.updatedAt, DHT_TIMESTAMP_SIZE);
    if (TIMESTAMP != QByteArray(DHT_TIMESTAMP_SIZE, '\0'))
    {
        record.updatedAt = QDateTime::fromString(QString::fromLatin1(TIMESTAMP), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!record.updatedAt.isValid())
            return false;
    }
    switch (data.memberType)
    {
    case DHT_MEMBER_STM32:
        record.memberType = QStringLiteral("STM32");
        break;
    case DHT_MEMBER_ARDUINO:
        record.memberType = QStringLiteral("ARDUINO");
        break;
    case DHT_MEMBER_PC:
        record.memberType = QStringLiteral("PC");
        break;
    default:
        break;
    }
    pendingRecords.insert(record.id, record);
    return true;
}

void ServerDhtQuery::HandleConnectionClosed()
{
    if (loading)
        FailQuery(tr("dht 조회 중 서버 연결이 종료되었습니다."), false);
    CancelQuery();
}

void ServerDhtQuery::HandleTimeout() { FailQuery(tr("dht 전체 조회 응답 시간이 초과되었습니다. 다시 접속해 주세요."), true); }

void ServerDhtQuery::FailQuery(const QString &message, bool closeConnection)
{
    if (!loading)
        return;
    loading = false;
    queryTimer->stop();
    pendingRecords.clear();
    if (closeConnection)
    {
        CancelQuery();
        connection->DisconnectFromServer();
    }
    emit QueryFailed(message);
}
