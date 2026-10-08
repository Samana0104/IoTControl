#include "PServerLogin.h"

#include "IoTPacketCodec.h"
#include "PServerConnection.h"

#include <QTimer>
#include <algorithm>

namespace
{
void ClearLoginFrame(uint8_t *buffer, size_t length)
{
    // 최적화로 지우는 코드가 생략되지 않도록 volatile로 씁니다.
    volatile uint8_t *bytes = buffer;
    for (size_t index = 0; index < length; ++index)
        bytes[index] = 0;
}
} // namespace

ServerLogin::ServerLogin(ServerConnection *connection, QObject *parent) : QObject(parent), connection(connection), loginTimer(new QTimer(this))
{
    loginTimer->setObjectName(QStringLiteral("loginResponseTimer"));
    loginTimer->setSingleShot(true);
    connect(loginTimer, &QTimer::timeout, this, &ServerLogin::HandleLoginTimeout);
    connect(connection, &ServerConnection::DataReceived, this, &ServerLogin::ReceiveLoginData);
    connect(connection, &ServerConnection::Disconnected, this, &ServerLogin::HandleConnectionClosed);
    connect(connection, &ServerConnection::ConnectionFailed, this, &ServerLogin::HandleConnectionClosed);
    connect(connection, &ServerConnection::Connected, this, &ServerLogin::CancelLogin);
}

void ServerLogin::LoginToServer(const MemData &member, int timeoutMs)
{
    if (loggingIn)
        return;
    // 같은 연결에서 수신 중인 센서 프레임의 경계는 로그인 재시도에도
    // 유지합니다.
    loggingIn = true;
    if (!connection->IsConnected())
    {
        FinishLogin(LOGIN_NO_SERVER);
        return;
    }

    uint8_t frame[HEADER_SIZE + MEM_DATA_SIZE]{};
    const size_t FRAME_LENGTH = MakeLoginPacket(frame, sizeof(frame), &member);
    if (FRAME_LENGTH == 0)
    {
        ClearLoginFrame(frame, sizeof(frame));
        FinishLogin(LOGIN_NO_SERVER);
        return;
    }

    // fromRawData는 이 함수의 write()에만 사용하고 신호에는 소유한 복사본을
    // 전달합니다.
    const QByteArray PACKET = QByteArray::fromRawData(reinterpret_cast<const char *>(frame), static_cast<qsizetype>(FRAME_LENGTH));
    loginTimer->start(std::max(1, timeoutMs));
    const bool SENT = connection->SendPacket(PACKET);
    if (SENT)
        emit LoginRequested(QByteArray(PACKET.constData(), PACKET.size()));
    ClearLoginFrame(frame, sizeof(frame));
    if (!SENT)
        FinishLogin(LOGIN_NO_SERVER);
}

void ServerLogin::CancelLogin()
{
    loggingIn = false;
    loginTimer->stop();
    receiveBuffer.clear();
    discardRemaining = 0;
}

bool ServerLogin::IsLoggingIn() const { return loggingIn; }

void ServerLogin::ReceiveLoginData(const QByteArray &data)
{
    qsizetype offset = 0;
    // 로그인 대기 전후에도 프레임을 소비하여 다음 요청이 payload 중간에서
    // 헤더를 읽지 않게 합니다. 인증 결과 판정은 loggingIn일 때만 수행합니다.
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
        if (loggingIn && header.cmd == ACK_LOGIN && CheckPacketLength(header.cmd, header.length) != 0)
        {
            FinishLogin(LOGIN_BAD_PACKET);
            return;
        }
        if (header.length > MAX_PAYLOAD_SIZE)
        {
            // 기다리는 ACK가 아닌 큰 프레임은 메모리를 늘리지 않고 분할
            // 폐기합니다.
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
        if (loggingIn && CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0)
        {
            FinishLogin(LOGIN_BAD_PACKET);
            return;
        }
        if (loggingIn && header.cmd == ACK_LOGIN)
        {
            ResultData result{};
            if (ReadResultData(frame + HEADER_SIZE, header.length, &result) != 0 || (result.result != RESULT_SUCCESS && result.result != RESULT_FAIL))
            {
                FinishLogin(LOGIN_BAD_PACKET);
                return;
            }
            else
                FinishLogin(result.result == RESULT_SUCCESS ? LOGIN_SUCCESS : LOGIN_REJECTED);
        }
        receiveBuffer.clear();
    }
}

void ServerLogin::HandleConnectionClosed()
{
    FinishLogin(LOGIN_NO_SERVER);
    CancelLogin();
}

void ServerLogin::HandleLoginTimeout() { FinishLogin(LOGIN_TIMEOUT); }

void ServerLogin::FinishLogin(LoginResult result)
{
    if (!loggingIn)
        return;
    CancelLogin();
    // 지연 ACK 또는 깨진 프레임이 다음 요청의 응답으로 쓰이지 않도록 접속을
    // 닫습니다.
    if (result == LOGIN_TIMEOUT || result == LOGIN_BAD_PACKET)
        connection->DisconnectFromServer();
    emit LoginFinished(result);
}
