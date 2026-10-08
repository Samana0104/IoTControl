#include "PServerSessionQuery.h"

#include "IoTPacketCodec.h"
#include "PServerConnection.h"

#include <QTimer>
#include <algorithm>

ServerSessionQuery::ServerSessionQuery(ServerConnection *connection, QObject *parent) : QObject(parent), connection(connection), queryTimer(new QTimer(this))
{
    queryTimer->setObjectName(QStringLiteral("sessionQueryTimer"));
    queryTimer->setSingleShot(true);
    connect(queryTimer, &QTimer::timeout, this, [this]
            { FailQuery(tr("현장 클라이언트 접속 상태 조회 시간이 초과되었습니다. 다시 접속해 주세요."), true); });
    connect(connection, &ServerConnection::DataReceived, this, &ServerSessionQuery::ReceiveData);
    connect(connection, &ServerConnection::Disconnected, this, &ServerSessionQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::ConnectionFailed, this, &ServerSessionQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::Connected, this, &ServerSessionQuery::CancelQuery);
}

void ServerSessionQuery::LoadSessions(int timeoutMs)
{
    if (loading)
        return;
    if (!connection->IsConnected())
    {
        emit QueryFailed(tr("접속 상태 조회 전에 서버에 접속해 주세요."));
        return;
    }
    uint8_t frame[HEADER_SIZE]{};
    const size_t LENGTH = MakeSessionAllPacket(frame, sizeof(frame));
    pendingRecords.clear();
    loading = true;
    queryTimer->start(std::max(1, timeoutMs));
    if (LENGTH == 0 || !connection->SendPacket(QByteArray(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(LENGTH))))
        FailQuery(tr("현장 클라이언트 접속 상태 조회 요청을 전송하지 못했습니다."), false);
}

void ServerSessionQuery::PauseQuery()
{
    loading = false;
    queryTimer->stop();
    pendingRecords.clear();
}

void ServerSessionQuery::CancelQuery()
{
    PauseQuery();
    receiveBuffer.clear();
    discardRemaining = 0;
}

bool ServerSessionQuery::IsLoading() const { return loading; }

void ServerSessionQuery::ReceiveData(const QByteArray &data)
{
    qsizetype offset = 0;
    // 로그인·DHT·팬 응답과 섞여 도착해도 프레임 경계를 유지합니다.
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
        const bool SESSION_FRAME = header.cmd == NFY_SESSION_ROW || header.cmd == ACK_SESSION_ALL;
        if (loading && SESSION_FRAME && CheckPacketLength(header.cmd, header.length) != 0)
        {
            FailQuery(tr("접속 상태 응답 길이가 올바르지 않습니다."), true);
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
        if (loading && SESSION_FRAME && CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0)
        {
            FailQuery(tr("접속 상태 응답 CRC가 올바르지 않습니다."), true);
            return;
        }
        if (loading && header.cmd == NFY_SESSION_ROW && !StoreSessionRow(frame + HEADER_SIZE, header.length))
        {
            FailQuery(tr("접속 상태 목록의 기기 정보가 올바르지 않습니다."), true);
            return;
        }
        if (loading && header.cmd == ACK_SESSION_ALL)
        {
            ResultData result{};
            if (ReadResultData(frame + HEADER_SIZE, header.length, &result) != 0 || (result.result != RESULT_SUCCESS && result.result != RESULT_FAIL))
            {
                FailQuery(tr("접속 상태 조회 완료 응답이 올바르지 않습니다."), true);
                return;
            }
            if (result.result == RESULT_FAIL)
                FailQuery(tr("서버가 현장 클라이언트 접속 상태 조회를 거절했습니다. PC 계정 권한을 확인해 주세요."), false);
            else
            {
                const SessionRecords RECORDS = pendingRecords.values();
                PauseQuery();
                receiveBuffer.clear();
                emit SessionsLoaded(RECORDS);
            }
        }
        receiveBuffer.clear();
    }
}

bool ServerSessionQuery::StoreSessionRow(const uint8_t *payload, size_t length)
{
    SessionRowData data{};
    if (ReadSessionRowData(payload, length, &data) != 0 || (data.memberType != SESSION_MEMBER_STM32 && data.memberType != SESSION_MEMBER_ARDUINO) || data.links == 0 || (data.links & ~(SESSION_LINK_TCP | SESSION_LINK_BT)) != 0)
        return false;
    const QByteArray ID(data.id, static_cast<qsizetype>(std::find(data.id, data.id + MEM_ID_SIZE, '\0') - data.id));
    const QString MEMBER_ID = QString::fromUtf8(ID);
    if (ID.isEmpty() || MEMBER_ID.toUtf8() != ID)
        return false;
    SessionRecord record;
    record.id = MEMBER_ID;
    record.memberType = data.memberType == SESSION_MEMBER_STM32 ? QStringLiteral("STM32") : QStringLiteral("ARDUINO");
    record.links = data.links;
    auto existing = pendingRecords.find(record.id);
    if (existing != pendingRecords.end())
    {
        if (existing->memberType != record.memberType)
            return false;
        existing->links |= record.links;
    }
    else
    {
        if (pendingRecords.size() >= SESSION_MAX_ROWS)
            return false;
        pendingRecords.insert(record.id, record);
    }
    return true;
}

void ServerSessionQuery::HandleConnectionClosed()
{
    if (loading)
        FailQuery(tr("현장 클라이언트 접속 상태 조회 중 서버 연결이 종료되었습니다."), false);
    CancelQuery();
}

void ServerSessionQuery::FailQuery(const QString &message, bool closeConnection)
{
    if (!loading)
        return;
    PauseQuery();
    if (closeConnection)
    {
        CancelQuery();
        connection->DisconnectFromServer();
    }
    emit QueryFailed(message);
}
