#pragma once

#include "PSessionRecord.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <cstddef>
#include <cstdint>

class ServerConnection;
class QTimer;

class ServerSessionQuery final : public QObject
{
    Q_OBJECT

  public:
    explicit ServerSessionQuery(ServerConnection *connection, QObject *parent = nullptr);
    void LoadSessions(int timeoutMs = 10000);
    void CancelQuery();
    void PauseQuery();
    bool IsLoading() const;

  signals:
    void SessionsLoaded(const SessionRecords &records);
    void QueryFailed(const QString &message);

  private:
    void ReceiveData(const QByteArray &data);
    bool StoreSessionRow(const uint8_t *payload, size_t length);
    void HandleConnectionClosed();
    void FailQuery(const QString &message, bool closeConnection);

    ServerConnection *connection;
    QTimer *queryTimer;
    QByteArray receiveBuffer;
    QMap<QString, SessionRecord> pendingRecords;
    qsizetype discardRemaining = 0;
    bool loading = false;
};
