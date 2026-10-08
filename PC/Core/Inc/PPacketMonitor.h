#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <cstddef>
#include <cstdint>
class ServerConnection;
class PacketMonitor final : public QObject
{
    Q_OBJECT
  public:
    explicit PacketMonitor(ServerConnection *connection, QObject *parent = nullptr);
  signals:
    void LineReady(const QString &line, bool sent, bool valid);

  private:
    void Observe(const QByteArray &data, bool sent);
    void Reset();
    QString Describe(uint16_t command, const uint8_t *payload, size_t length) const;
    QByteArray receiveBuffer;
    QByteArray sendBuffer;
    qsizetype receiveDiscard = 0;
    qsizetype sendDiscard = 0;
};
