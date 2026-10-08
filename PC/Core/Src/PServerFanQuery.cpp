#include "PServerFanQuery.h"

#include "IoTPacketCodec.h"
#include "PServerConnection.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <algorithm>
#include <cstring>

namespace
{
constexpr int MAX_FAN_PERCENT = 100;

QString DescribeApplyFailure(uint8_t reason)
{
    switch (reason)
    {
    case FAN_APPLY_INVALID_TARGET:
        return QObject::tr("팬 제어 대상 ID가 올바르지 않거나 같은 ID의 BT 연결이 중복되었습니다.");
    case FAN_APPLY_NOT_CONNECTED:
        return QObject::tr("선택한 STM32가 블루투스로 접속하지 않았습니다. 접속 상태를 확인해 주세요.");
    case FAN_APPLY_DB_ERROR:
        return QObject::tr("서버에서 저장된 팬 속도를 확인하지 못했습니다.");
    case FAN_APPLY_DB_CHANGED:
        return QObject::tr("DB 팬 속도가 다른 값으로 변경되어 적용을 중단했습니다. DB를 다시 확인해 주세요.");
    case FAN_APPLY_BUSY:
        return QObject::tr("다른 팬 제어 요청의 응답을 기다리고 있습니다. 잠시 후 다시 시도해 주세요.");
    case FAN_APPLY_SEND_FAILED:
        return QObject::tr("서버가 팬 제어 요청을 장치에 전송하지 못했습니다.");
    case FAN_APPLY_REJECTED:
        return QObject::tr("STM32가 팬 속도 적용을 거절했습니다.");
    case FAN_APPLY_TIMEOUT:
        return QObject::tr("STM32의 팬 적용 응답 시간이 초과되었습니다. BT 연결을 다시 확인해 주세요.");
    case FAN_APPLY_DISCONNECTED:
        return QObject::tr("팬 응답을 기다리는 중 STM32의 BT 연결이 종료되었습니다.");
    case FAN_APPLY_NOT_ALLOWED:
        return QObject::tr("팬 적용은 로그인한 PC 계정에서만 요청할 수 있습니다.");
    default:
        return QObject::tr("팬 적용 응답을 확인하지 못했습니다.");
    }
}
} // namespace

ServerFanQuery::ServerFanQuery(ServerConnection *connection, QObject *parent) : QObject(parent), connection(connection), queryTimer(new QTimer(this))
{
    queryTimer->setObjectName(QStringLiteral("fanQueryTimer"));
    queryTimer->setSingleShot(true);
    connect(queryTimer, &QTimer::timeout, this,
            [this]
            {
                const QString DB_MESSAGE = updating ? tr("팬 DB 저장 응답 시간이 초과되었습니다. 저장 여부를 다시 조회해 주세요.") : tr("팬 속도 조회 응답 시간이 초과되었습니다. 다시 접속해 주세요.");
                const QString MESSAGE = applying ? tr("팬 장치 적용 응답 시간이 초과되었습니다. 적용 여부를 확인해 주세요.") : DB_MESSAGE;
                FailQuery(MESSAGE, true);
            });
    connect(connection, &ServerConnection::DataReceived, this, &ServerFanQuery::ReceiveData);
    connect(connection, &ServerConnection::Disconnected, this, &ServerFanQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::ConnectionFailed, this, &ServerFanQuery::HandleConnectionClosed);
    connect(connection, &ServerConnection::Connected, this, &ServerFanQuery::CancelQuery);
    WriteDiagnostic(QStringLiteral("start executable=%1 built=%2 %3 request=0x000E ack=0x800E").arg(QCoreApplication::applicationFilePath(), QString::fromLatin1(__DATE__), QString::fromLatin1(__TIME__)));
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
    WriteDiagnostic(QStringLiteral("send query cmd=0x000A length=0"));
    if (LENGTH == 0 || !connection->SendPacket(QByteArray(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(LENGTH))))
        FailQuery(tr("팬 속도 조회 요청을 전송하지 못했습니다."), false);
}

void ServerFanQuery::UpdateFanSpeed(int percent, int timeoutMs)
{
    if (loading)
        return;
    if (!connection->IsConnected())
    {
        emit UpdateFailed(tr("팬 DB 저장 전에 서버에 접속해 주세요."));
        return;
    }
    if (percent < 0 || percent > MAX_FAN_PERCENT)
    {
        emit UpdateFailed(tr("팬 속도는 0–100% 범위로 입력해 주세요."));
        return;
    }
    const FanData FAN = {static_cast<uint16_t>(percent)};
    uint8_t frame[HEADER_SIZE + FAN_DATA_SIZE]{};
    const size_t LENGTH = MakeFanUpdatePacket(frame, sizeof(frame), &FAN);
    requestedPercent = percent;
    updating = true;
    loading = true;
    queryTimer->start(std::max(1, timeoutMs));
    WriteDiagnostic(QStringLiteral("send update cmd=0x000E length=%1 percent=%2 timeoutMs=%3").arg(FAN_DATA_SIZE).arg(percent).arg(timeoutMs));
    if (LENGTH == 0 || !connection->SendPacket(QByteArray(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(LENGTH))))
        FailQuery(tr("팬 DB 저장 요청을 전송하지 못했습니다."), false);
}

void ServerFanQuery::CancelQuery()
{
    PauseQuery();
    receiveBuffer.clear();
    discardRemaining = 0;
}

void ServerFanQuery::ApplySavedSpeed(const QString &clientId, int percent, int timeoutMs)
{
    if (loading)
        return;
    if (!connection->IsConnected())
    {
        emit ApplyFailed(clientId, tr("팬 적용 전에 서버에 접속해 주세요."));
        return;
    }
    const QByteArray ID = clientId.toUtf8();
    if (ID.isEmpty() || ID.size() > MEM_ID_SIZE || ID.contains('\0') || QString::fromUtf8(ID) != clientId || percent < 0 || percent > MAX_FAN_PERCENT)
    {
        emit ApplyFailed(clientId, tr("팬 제어 대상 ID 또는 저장된 속도가 올바르지 않습니다."));
        return;
    }
    FanApplyData request{};
    std::memcpy(request.id, ID.constData(), static_cast<size_t>(ID.size()));
    request.fan.fanSpeed = static_cast<uint16_t>(percent);
    uint8_t frame[HEADER_SIZE + FAN_APPLY_DATA_SIZE]{};
    const size_t LENGTH = MakeFanApplyPacket(frame, sizeof(frame), &request);
    requestedClientId = clientId;
    requestedPercent = percent;
    applying = true;
    loading = true;
    queryTimer->start(std::max(1, timeoutMs));
    WriteDiagnostic(QStringLiteral("send apply cmd=0x0010 id=%1 percent=%2").arg(clientId).arg(percent));
    if (LENGTH == 0 || !connection->SendPacket(QByteArray(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(LENGTH))))
        FailQuery(tr("팬 장치 적용 요청을 전송하지 못했습니다."), false);
}

void ServerFanQuery::PauseQuery()
{
    loading = false;
    updating = false;
    applying = false;
    queryTimer->stop();
}

bool ServerFanQuery::IsLoading() const { return loading; }

bool ServerFanQuery::IsApplying() const { return applying; }

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
        if (loading && receiveBuffer.size() == HEADER_SIZE)
            WriteDiagnostic(QStringLiteral("receive header cmd=0x%1 length=%2").arg(header.cmd, 4, 16, QLatin1Char('0')).arg(header.length));
        const uint16_t DB_ACK = updating ? ACK_FAN_UPDATE : ACK_FAN_QUERY;
        const uint16_t EXPECTED_ACK = applying ? ACK_FAN_APPLY : DB_ACK;
        if (loading && header.cmd == EXPECTED_ACK && CheckPacketLength(header.cmd, header.length) != 0)
        {
            FailQuery(tr("팬 응답 길이가 올바르지 않습니다."), true);
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
        if (loading && applying && header.cmd == ACK_FAN_APPLY)
        {
            const auto *frame = reinterpret_cast<const uint8_t *>(receiveBuffer.constData());
            FanApplyAckData result{};
            if (CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0 || ReadFanApplyAckData(frame + HEADER_SIZE, header.length, &result) != 0 || result.reason > FAN_APPLY_NOT_ALLOWED || result.fan.fanSpeed > MAX_FAN_PERCENT || result.result != (result.reason == FAN_APPLY_OK ? RESULT_SUCCESS : RESULT_FAIL) || (result.result == RESULT_SUCCESS && result.fan.fanSpeed != requestedPercent))
            {
                FailQuery(tr("팬 장치 적용 응답이 올바르지 않습니다. 적용 여부를 확인해 주세요."), true);
                return;
            }
            const QString ID = requestedClientId;
            WriteDiagnostic(QStringLiteral("apply ack id=%1 result=%2 reason=%3 percent=%4").arg(ID).arg(result.result).arg(result.reason).arg(result.fan.fanSpeed));
            PauseQuery();
            receiveBuffer.clear();
            if (result.result == RESULT_SUCCESS)
                emit FanApplied(ID, result.fan.fanSpeed);
            else
                emit ApplyFailed(ID, DescribeApplyFailure(result.reason));
        }
        else if (loading && updating && header.cmd == ACK_FAN_UPDATE)
        {
            const auto *frame = reinterpret_cast<const uint8_t *>(receiveBuffer.constData());
            ResultData result{};
            if (CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0 || ReadResultData(frame + HEADER_SIZE, header.length, &result) != 0 || (result.result != RESULT_SUCCESS && result.result != RESULT_FAIL))
            {
                FailQuery(tr("팬 DB 저장 응답이 올바르지 않습니다. 저장 여부를 다시 조회해 주세요."), true);
                return;
            }
            const int PERCENT = requestedPercent;
            WriteDiagnostic(QStringLiteral("update ack result=%1 percent=%2").arg(result.result).arg(PERCENT));
            PauseQuery();
            receiveBuffer.clear();
            if (result.result == RESULT_SUCCESS)
                emit FanUpdated(PERCENT);
            else
                emit UpdateFailed(tr("서버의 fan UPDATE가 실패했습니다. singleton_id=1 행과 서버 DB 설정을 확인해 주세요."));
        }
        else if (loading && !updating && header.cmd == ACK_FAN_QUERY)
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
    {
        const QString DB_MESSAGE = updating ? tr("팬 DB 저장 중 서버 연결이 종료되었습니다. 저장 여부를 다시 조회해 주세요.") : tr("팬 속도 조회 중 서버 연결이 종료되었습니다.");
        const QString MESSAGE = applying ? tr("팬 장치 적용 중 서버 연결이 종료되었습니다. 적용 여부를 확인해 주세요.") : DB_MESSAGE;
        FailQuery(MESSAGE, false);
    }
    CancelQuery();
}

void ServerFanQuery::FailQuery(const QString &message, bool closeConnection)
{
    if (!loading)
        return;
    const bool WAS_UPDATING = updating;
    const bool WAS_APPLYING = applying;
    const QString ID = requestedClientId;
    const QString DB_OPERATION = updating ? QStringLiteral("update") : QStringLiteral("query");
    const QString OPERATION = applying ? QStringLiteral("apply") : DB_OPERATION;
    WriteDiagnostic(QStringLiteral("failure operation=%1 closeRequested=%2 connected=%3 reason=%4").arg(OPERATION).arg(closeConnection).arg(connection->IsConnected()).arg(message));
    PauseQuery();
    if (closeConnection)
    {
        receiveBuffer.clear();
        discardRemaining = 0;
        connection->DisconnectFromServer();
    }
    if (WAS_APPLYING)
        emit ApplyFailed(ID, message);
    else if (WAS_UPDATING)
        emit UpdateFailed(message);
    else
        emit QueryFailed(message);
}

void ServerFanQuery::WriteDiagnostic(const QString &message) const
{
    // 로그인/비밀번호 및 수신 payload는 기록하지 않습니다.
    QFile log(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fan-connection.log")));
    const QIODevice::OpenMode MODE = QIODevice::WriteOnly | (log.size() > 1024 * 1024 ? QIODevice::Truncate : QIODevice::Append);
    if (log.open(MODE))
        log.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + QLatin1Char(' ') + message + QLatin1Char('\n')).toUtf8());
}
