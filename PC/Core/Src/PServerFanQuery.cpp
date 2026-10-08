#include "PServerFanQuery.h"

#include "IoTPacketCodec.h"
#include "PServerConnection.h"

#include <QTimer>
#include <algorithm>

namespace
{
constexpr int MAX_FAN_PERCENT = 100;
} // namespace

ServerFanQuery::ServerFanQuery(ServerConnection *connection, QObject *parent) : QObject(parent), connection(connection), queryTimer(new QTimer(this))
{
    queryTimer->setObjectName(QStringLiteral("fanQueryTimer"));
    queryTimer->setSingleShot(true);
    connect(queryTimer, &QTimer::timeout, this, [this]
            { FailQuery(tr("팬 속도 조회 응답 시간이 초과되었습니다. 다시 접속해 주세요."), true); });
    connect(connection, &ServerConnection::DataReceived, this, &ServerFanQuery::ReceiveData);
    connect(connection, &ServerConnection::Disconnected, this, &ServerFanQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::ConnectionFailed, this, &ServerFanQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::Connected, this, &ServerFanQuery::CancelQuery);
}

void ServerFanQuery::LoadFanSpeed(int timeoutMs)
{
    if (loading)
        return;
    if (!connection->IsConnected())
    {
        emit QueryFailed(tr("팬 속도 조회 전에 서버에 접속해 주세요."));
        return;
    }
    uint8_t frame[HEADER_SIZE]{};
    const size_t LENGTH = MakeFanQueryPacket(frame, sizeof(frame));
    loading = true;
    queryTimer->start(std::max(1, timeoutMs));
    if (LENGTH == 0 || !connection->SendPacket(QByteArray(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(LENGTH))))
        FailQuery(tr("팬 속도 조회 요청을 전송하지 못했습니다."), false);
}

void ServerFanQuery::CancelQuery()
{
    PauseQuery();
    receiveBuffer.clear();
    discardRemaining = 0;
}

void ServerFanQuery::PauseQuery()
{
    loading = false;
    queryTimer->stop();
}

bool ServerFanQuery::IsLoading() const { return loading; }

void ServerFanQuery::ReceiveData(const QByteArray &data)
{
    qsizetype offset = 0;
    // 조회 대기 전후에도 TCP 프레임 경계를 유지하여 DHT·로그인 응답을 건너뜁니다.
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
        if (loading && header.cmd == ACK_FAN_QUERY && CheckPacketLength(header.cmd, header.length) != 0)
        {
            FailQuery(tr("팬 속도 조회 응답 길이가 올바르지 않습니다."), true);
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
        if (loading && header.cmd == ACK_FAN_QUERY)
        {
            const auto *frame = reinterpret_cast<const uint8_t *>(receiveBuffer.constData());
            FanQueryAckData result{};
            if (CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0 || ReadFanQueryAckData(frame + HEADER_SIZE, header.length, &result) != 0 || (result.result != RESULT_SUCCESS && result.result != RESULT_FAIL) || (result.result == RESULT_SUCCESS && result.fan.fanSpeed > MAX_FAN_PERCENT))
            {
                FailQuery(tr("팬 속도 조회 응답이 올바르지 않습니다."), true);
                return;
            }
            loading = false;
            queryTimer->stop();
            receiveBuffer.clear();
            if (result.result == RESULT_SUCCESS)
                emit FanLoaded(result.fan.fanSpeed);
            else
                emit QueryFailed(tr("서버의 fan 조회가 실패했습니다. singleton_id=1 행과 서버 DB 설정을 확인해 주세요."));
        }
        receiveBuffer.clear();
    }
}

void ServerFanQuery::HandleConnectionClosed()
{
    if (loading)
        FailQuery(tr("팬 속도 조회 중 서버 연결이 종료되었습니다."), false);
    CancelQuery();
}

void ServerFanQuery::FailQuery(const QString &message, bool closeConnection)
{
    if (!loading)
        return;
    PauseQuery();
    if (closeConnection)
    {
        receiveBuffer.clear();
        discardRemaining = 0;
        connection->DisconnectFromServer();
    }
    emit QueryFailed(message);
}
