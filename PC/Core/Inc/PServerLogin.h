#pragma once

#include "IoTPacket.h"
#include "PLoginResult.h"

#include <QByteArray>
#include <QObject>
#include <QtTypes>

class ServerConnection;
class QTimer;

// 공용 로그인 패킷 송신과 비동기 ACK_LOGIN 수신만 담당합니다.
class ServerLogin final : public QObject
{
    Q_OBJECT

  public:
    explicit ServerLogin(ServerConnection *connection, QObject *parent = nullptr);
    void LoginToServer(const MemData &member, int timeoutMs = 5000);
    void CancelLogin();
    bool IsLoggingIn() const;

  signals:
    void LoginFinished(LoginResult result);
    // 송신 관찰용입니다. 이 신호에서 패킷을 다시 전송하지 않습니다.
    void LoginRequested(const QByteArray &packet);

  private:
    void ReceiveLoginData(const QByteArray &data);
    void HandleConnectionClosed();
    void HandleLoginTimeout();
    void FinishLogin(LoginResult result);

    ServerConnection *connection;
    QTimer *loginTimer;
    QByteArray receiveBuffer;
    qsizetype discardRemaining = 0;
    bool loggingIn = false;
};
