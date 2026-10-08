#include "PServerConnection.h"

#include <QTcpSocket>
#include <QTimer>

namespace
{
constexpr int SERVER_CONNECTION_TIMEOUT_MS = 10000;
} // namespace

ServerConnection::ServerConnection(QObject *parent)
    : QObject(parent), socket(new QTcpSocket(this)),
      connectionTimer(new QTimer(this))
{
    socket->setObjectName(QStringLiteral("serverSocket"));
    connectionTimer->setObjectName(QStringLiteral("serverConnectionTimer"));
    connectionTimer->setSingleShot(true);
    connectionTimer->setInterval(SERVER_CONNECTION_TIMEOUT_MS);
    connect(socket, &QTcpSocket::connected, this,
            &ServerConnection::HandleConnected);
    connect(socket, &QTcpSocket::disconnected, this,
            &ServerConnection::HandleDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this,
            &ServerConnection::HandleSocketError);
    connect(socket, &QTcpSocket::readyRead, this,
            &ServerConnection::ReadServerData);
    connect(connectionTimer, &QTimer::timeout, this,
            &ServerConnection::HandleConnectionTimeout);
}

ServerConnection::~ServerConnection() { DisconnectFromServer(); }

void ServerConnection::ConnectToServer(const QString &host, quint16 port)
{
    DisconnectFromServer();
    if (host.trimmed().isEmpty() || port == 0)
    {
        emit ConnectionFailed(tr("서버 주소와 포트를 확인해 주세요."));
        return;
    }
    active = true;
    connectionTimer->start();
    // 이벤트 루프에서 DNS 조회와 TCP 접속을 수행하므로 UI를 막지 않습니다.
    socket->connectToHost(host.trimmed(), port);
}

void ServerConnection::DisconnectFromServer()
{
    // 사용자가 취소하거나 서버를 바꾼 경우 종료/오류 안내를 발생시키지
    // 않습니다.
    active = false;
    connected = false;
    connectionTimer->stop();
    socket->abort();
}

bool ServerConnection::IsConnected() const
{
    return active && connected &&
           socket->state() == QAbstractSocket::ConnectedState;
}

bool ServerConnection::IsConnecting() const { return active && !connected; }

bool ServerConnection::SendPacket(const QByteArray &packet)
{
    if (!IsConnected() || packet.isEmpty())
        return false;
    if (socket->write(packet) != packet.size())
    {
        FailConnection(tr("서버로 요청을 전송하지 못했습니다: %1")
                           .arg(socket->errorString()));
        return false;
    }
    // write() 성공은 송신 버퍼 등록을 뜻합니다. 인증 성공 판정은 서버 ACK로
    // 합니다.
    return true;
}

void ServerConnection::HandleConnected()
{
    if (!active)
        return;
    connected = true;
    connectionTimer->stop();
    emit Connected();
}

void ServerConnection::HandleDisconnected()
{
    if (!active)
        return;
    active = false;
    connected = false;
    connectionTimer->stop();
    emit Disconnected();
}

void ServerConnection::HandleSocketError()
{
    if (active && connected &&
        socket->error() == QAbstractSocket::RemoteHostClosedError)
    {
        // 종료 직전 도착한 바이트도 수신부에 전달한 뒤 연결 종료를 알립니다.
        ReadServerData();
        DisconnectFromServer();
        emit Disconnected();
        return;
    }
    if (active)
        FailConnection(socket->errorString());
}

void ServerConnection::HandleConnectionTimeout()
{
    if (IsConnecting())
        FailConnection(tr("서버 접속 시간이 초과되었습니다. 주소와 포트, 서버 "
                          "실행 상태를 확인해 주세요. (10초)"));
}

void ServerConnection::ReadServerData()
{
    const QByteArray DATA = socket->readAll();
    if (IsConnected() && !DATA.isEmpty())
        emit DataReceived(DATA);
}

void ServerConnection::FailConnection(const QString &message)
{
    DisconnectFromServer();
    emit ConnectionFailed(message);
}
