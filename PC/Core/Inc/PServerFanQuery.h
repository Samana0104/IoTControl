#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtTypes>

class ServerConnection;
class QTimer;

// 서버 DB의 단일 팬 속도를 비동기로 조회·저장합니다. 장치 제어는 별도 기능입니다.
class ServerFanQuery final : public QObject
{
    Q_OBJECT

  public:
    explicit ServerFanQuery(ServerConnection *connection, QObject *parent = nullptr);
    void LoadFanSpeed(int timeoutMs = 10000);
    void UpdateFanSpeed(int percent, int timeoutMs = 10000);
    void CancelQuery();
    void PauseQuery();
    bool IsLoading() const;

  signals:
    void FanLoaded(int percent);
    void QueryFailed(const QString &message);
    void FanUpdated(int percent);
    void UpdateFailed(const QString &message);

  private:
    void ReceiveData(const QByteArray &data);
    void HandleConnectionClosed();
    void FailQuery(const QString &message, bool closeConnection);

    ServerConnection *connection;
    QTimer *queryTimer;
    QByteArray receiveBuffer;
    qsizetype discardRemaining = 0;
    bool loading = false;
    bool updating = false;
    int requestedPercent = 0;
};
