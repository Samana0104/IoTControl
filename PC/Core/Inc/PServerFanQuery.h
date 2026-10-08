#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtTypes>

class ServerConnection;
class QTimer;

// 단일 팬 DB 조회·저장 및 선택한 STM32(BT)에 저장된 값 적용을 비동기로 처리합니다.
class ServerFanQuery final : public QObject
{
    Q_OBJECT

  public:
    explicit ServerFanQuery(ServerConnection *connection, QObject *parent = nullptr);
    void LoadFanSpeed(int timeoutMs = 10000);
    void UpdateFanSpeed(int percent, int timeoutMs = 10000);
    void ApplySavedSpeed(const QString &clientId, int percent, int timeoutMs = 10000);
    void CancelQuery();
    void PauseQuery();
    bool IsLoading() const;
    bool IsApplying() const;

  signals:
    void FanLoaded(int percent);
    void QueryFailed(const QString &message);
    void FanUpdated(int percent);
    void UpdateFailed(const QString &message);
    void FanApplied(const QString &clientId, int percent);
    void ApplyFailed(const QString &clientId, const QString &message);

  private:
    void ReceiveData(const QByteArray &data);
    void HandleConnectionClosed();
    void FailQuery(const QString &message, bool closeConnection);
    void WriteDiagnostic(const QString &message) const;

    ServerConnection *connection;
    QTimer *queryTimer;
    QByteArray receiveBuffer;
    qsizetype discardRemaining = 0;
    bool loading = false;
    bool updating = false;
    bool applying = false;
    int requestedPercent = 0;
    QString requestedClientId;
};
