#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>
#include <cstdint>
class ServerConnection;
class QTimer;
class ServerBluetooth final : public QObject
{
    Q_OBJECT
  public:
    explicit ServerBluetooth(ServerConnection *connection, QObject *parent = nullptr);
    void StartScan(int timeoutMs = 45000);
    void ConnectAll(int timeoutMs = 90000);
    bool IsBusy() const;
    bool IsScanning() const;
    const QList<QStringList> &ReadRows() const;
    QString ReadFeedback() const;
    void Cancel();
  signals:
    void Changed();
    void Completed();

  private:
    void Start(uint16_t command, int timeoutMs);
    void ReceiveData(const QByteArray &data);
    void Finish(const QString &message, bool closeConnection = false);
    ServerConnection *connection;
    QTimer *timer;
    QByteArray buffer;
    qsizetype discardRemaining = 0;
    QList<QStringList> rows;
    QString feedback;
    uint16_t pendingCommand = 0;
    bool scanning = true;
    int responseTimeoutMs = 45000;
};
