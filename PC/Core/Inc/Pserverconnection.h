#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtTypes>

class QTcpSocket;
class QTimer;

// TCP 연결과 바이트 송수신만 담당합니다. UI와 로그인 인증 판정은 별도입니다.
class ServerConnection final : public QObject
{
    Q_OBJECT

  public:
    explicit ServerConnection(QObject *parent = nullptr);
    ~ServerConnection() override;
    void ConnectToServer(const QString &host, quint16 port);
    void DisconnectFromServer();
    bool IsConnected() const;
    bool IsConnecting() const;
    bool SendPacket(const QByteArray &packet);

  signals:
    void Connected();
    void Disconnected();
    void ConnectionFailed(const QString &message);
    // TCP 청크입니다. 수신부에서 공용 헤더 기준으로 프레임을 조립해야 합니다.
    void DataReceived(const QByteArray &data);

  private:
    void HandleConnected();
    void HandleDisconnected();
    void HandleSocketError();
    void HandleConnectionTimeout();
    void ReadServerData();
    void FailConnection(const QString &message);

    QTcpSocket *socket;
    QTimer *connectionTimer;
    bool active = false;
    bool connected = false;
};
