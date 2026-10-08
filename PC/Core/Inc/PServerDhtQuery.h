#pragma once

#include "PDhtRecord.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QtTypes>
#include <cstddef>
#include <cstdint>

class ServerConnection;
class QTimer;

class ServerDhtQuery final : public QObject
{
    Q_OBJECT

  public:
    explicit ServerDhtQuery(ServerConnection *connection, QObject *parent = nullptr);
    void LoadAllDht(int timeoutMs = 10000);
    void RequestDhtCollect(int timeoutMs = 10000);
    void RequestClientRefresh(const QString &clientId, int timeoutMs = 10000);
    bool IsRefreshing() const;
    void CancelQuery();
    void PauseQuery();
    bool IsLoading() const;
    bool IsCollecting() const;
    bool IsBusy() const;

  signals:
    void DhtLoaded(const DhtRecords &records);
    void QueryFailed(const QString &message);
    void CollectFinished(bool requested);
    void RefreshFinished(const QString &clientId);
    void RefreshFailed(const QString &clientId, const QString &message);

  private:
    void ReceiveData(const QByteArray &data);
    bool StoreDhtRow(const uint8_t *payload, size_t length);
    void HandleConnectionClosed();
    void HandleTimeout();
    void FailQuery(const QString &message, bool closeConnection);

    ServerConnection *connection;
    QTimer *queryTimer;
    QByteArray receiveBuffer;
    QMap<QString, DhtRecord> pendingRecords;
    qsizetype discardRemaining = 0;
    bool loading = false;
    bool collecting = false;
    bool refreshing = false;
    QString refreshClientId;
};
