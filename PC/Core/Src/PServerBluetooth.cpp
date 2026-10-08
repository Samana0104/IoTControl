#include "PServerBluetooth.h"
#include "IoTPacketCodec.h"
#include "PServerConnection.h"
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

ServerBluetooth::ServerBluetooth(ServerConnection *connection, QObject *parent) : QObject(parent), connection(connection), timer(new QTimer(this))
{
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this]
            { Finish(tr("블루투스 서버 응답 시간 초과. 다시 접속해 주세요."), true); });
    connect(connection, &ServerConnection::DataReceived, this, &ServerBluetooth::ReceiveData);
    connect(connection, &ServerConnection::Disconnected, this,
            [this]
            {
                if (IsBusy())
                    Finish(tr("서버 연결이 종료되었습니다."));
                Cancel();
            });
    connect(connection, &ServerConnection::Connected, this, &ServerBluetooth::Cancel);
}
bool ServerBluetooth::IsBusy() const { return pendingCommand != 0; }
bool ServerBluetooth::IsScanning() const { return scanning; }
const QList<QStringList> &ServerBluetooth::ReadRows() const { return rows; }
QString ServerBluetooth::ReadFeedback() const { return feedback; }
void ServerBluetooth::StartScan(int timeoutMs) { Start(REQ_BT_SCAN, timeoutMs); }
void ServerBluetooth::ConnectAll(int timeoutMs) { Start(REQ_BT_CONNECT_ALL, timeoutMs); }
void ServerBluetooth::Cancel()
{
    pendingCommand = 0;
    timer->stop();
    buffer.clear();
    discardRemaining = 0;
    rows.clear();
    emit Changed();
}
void ServerBluetooth::Start(uint16_t command, int timeoutMs)
{
    if (IsBusy())
        return;
    if (!connection->IsConnected())
    {
        feedback = tr("서버에 로그인한 후 사용할 수 있습니다.");
        emit Changed();
        return;
    }
    scanning = command == REQ_BT_SCAN;
    rows.clear();
    pendingCommand = command;
    responseTimeoutMs = std::max(1, timeoutMs);
    feedback = scanning ? tr("Raspberry에서 주변 장치를 10초 동안 검색합니다…") : tr("DB 등록 기기를 연결합니다. 기기별 결과를 기다립니다…");
    timer->start(responseTimeoutMs);
    emit Changed();
    uint8_t frame[HEADER_SIZE]{};
    const size_t LENGTH = MakeBtOperationPacket(frame, sizeof(frame), command);
    if (LENGTH == 0 || !connection->SendPacket(QByteArray(reinterpret_cast<const char *>(frame), LENGTH)))
        Finish(tr("블루투스 요청 전송 실패"));
}
void ServerBluetooth::Finish(const QString &message, bool closeConnection)
{
    pendingCommand = 0;
    timer->stop();
    feedback = message;
    if (closeConnection)
        connection->DisconnectFromServer();
    emit Changed();
    emit Completed();
}
void ServerBluetooth::ReceiveData(const QByteArray &data)
{
    qsizetype offset = 0;
    while (connection->IsConnected() && offset < data.size())
    {
        if (discardRemaining > 0)
        {
            const qsizetype COUNT = std::min(discardRemaining, data.size() - offset);
            discardRemaining -= COUNT;
            offset += COUNT;
            continue;
        }
        if (buffer.size() < HEADER_SIZE)
        {
            const qsizetype COUNT = std::min(HEADER_SIZE - buffer.size(), data.size() - offset);
            buffer.append(data.constData() + offset, COUNT);
            offset += COUNT;
            if (buffer.size() < HEADER_SIZE)
                return;
        }
        HeaderData header{};
        DecodePacketHeader(reinterpret_cast<const uint8_t *>(buffer.constData()), &header);
        const bool ROW = IsBusy() && header.cmd == MAKE_NOTIFY(pendingCommand);
        const bool ACK = IsBusy() && header.cmd == REQ_TO_ACK(pendingCommand);
        if ((ROW || ACK) && CheckPacketLength(header.cmd, header.length) != 0)
        {
            Finish(tr("블루투스 응답 길이 오류"), true);
            return;
        }
        if (header.length > MAX_PAYLOAD_SIZE)
        {
            buffer.clear();
            discardRemaining = header.length;
            continue;
        }
        const qsizetype TOTAL = HEADER_SIZE + header.length;
        const qsizetype COUNT = std::min(TOTAL - buffer.size(), data.size() - offset);
        buffer.append(data.constData() + offset, COUNT);
        offset += COUNT;
        if (buffer.size() < TOTAL)
            return;
        const auto *frame = reinterpret_cast<const uint8_t *>(buffer.constData());
        if ((ROW || ACK) && CheckPacketCrc(frame, &header, frame + HEADER_SIZE) != 0)
        {
            Finish(tr("블루투스 응답 CRC 오류"), true);
            return;
        }
        if (ROW)
        {
            QStringList row;
            if (rows.size() >= (scanning ? BT_SCAN_MAX_ROWS : BT_CONNECT_ALL_MAX_ROWS))
            {
                Finish(tr("블루투스 결과 수 초과"), true);
                return;
            }
            if (scanning)
            {
                BtScanRowData value{};
                if (ReadBtScanRowData(frame + HEADER_SIZE, header.length, &value) != 0 || value.paired > 1)
                {
                    Finish(tr("스캔 결과 형식 오류"), true);
                    return;
                }
                const QString MAC = QString::fromLatin1(value.mac, BLUETOOTH_MAC_SIZE);
                const QByteArray NAME(value.name, std::find(value.name, value.name + BT_SCAN_NAME_SIZE, '\0') - value.name);
                if (!QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")).match(MAC).hasMatch() || QString::fromUtf8(NAME).toUtf8() != NAME)
                {
                    Finish(tr("스캔 주소 또는 이름 형식 오류"), true);
                    return;
                }
                row = {MAC, NAME.isEmpty() ? tr("이름 없음") : QString::fromUtf8(NAME), QString::number(value.rssi), value.paired ? tr("페어링됨") : tr("미페어링")};
            }
            else
            {
                BtConnectRowData value{};
                if (ReadBtConnectRowData(frame + HEADER_SIZE, header.length, &value) != 0 || value.status > BT_CONNECT_FAILED)
                {
                    Finish(tr("연결 결과 형식 오류"), true);
                    return;
                }
                const QByteArray ID(value.id, std::find(value.id, value.id + MEM_ID_SIZE, '\0') - value.id);
                if (ID.isEmpty() || QString::fromUtf8(ID).toUtf8() != ID)
                {
                    Finish(tr("연결 대상 ID 오류"), true);
                    return;
                }
                const QStringList STATES{tr("연결 성공"), tr("이미 연결됨"), tr("DB 미등록"), tr("연결 실패")};
                row = {QString::fromUtf8(ID), STATES.at(value.status)};
            }
            rows.append(row);
            timer->start(responseTimeoutMs); // 전체 연결은 기기별 결과가 올 때마다 대기 시간 갱신.
            emit Changed();
        }
        if (ACK)
        {
            BtOperationAckData value{};
            if (ReadBtOperationAckData(frame + HEADER_SIZE, header.length, &value) != 0 || value.reason > BT_OPERATION_LIMIT || (value.result != RESULT_SUCCESS && value.result != RESULT_FAIL) || ((value.result == RESULT_SUCCESS) != (value.reason == BT_OPERATION_OK)))
            {
                Finish(tr("블루투스 완료 응답 형식 오류"), true);
                return;
            }
            if (value.reason == BT_OPERATION_BUSY)
                Finish(tr("서버가 다른 블루투스 작업을 진행 중입니다."));
            else if (value.reason == BT_OPERATION_NOT_ALLOWED)
                Finish(tr("PC 계정 로그인 권한이 필요합니다."));
            else if (value.reason == BT_OPERATION_FAILED)
                Finish(scanning ? tr("서버 Bluetooth 검색 실패. 어댑터·BlueZ 상태를 확인해 주세요.") : tr("DB 등록 목록 조회 또는 연결 작업 실패"));
            else if (value.reason == BT_OPERATION_LIMIT)
                Finish(tr("최대 %1개 결과를 표시했습니다. 일부 결과가 생략될 수 있습니다.").arg(scanning ? BT_SCAN_MAX_ROWS : BT_CONNECT_ALL_MAX_ROWS));
            else if (rows.isEmpty())
                Finish(scanning ? tr("검색 완료 · 주변 장치가 없습니다.") : tr("전체 연결 완료 · DB에 등록된 Bluetooth 기기가 없습니다."));
            else if (scanning)
                Finish(tr("검색 완료 · %1개 장치").arg(rows.size()));
            else
            {
                int succeeded = 0;
                for (const QStringList &row : rows)
                    if (row[1] == tr("연결 성공") || row[1] == tr("이미 연결됨"))
                        ++succeeded;
                Finish(tr("전체 연결 완료 · %1/%2대 연결 · 실패 %3대").arg(succeeded).arg(rows.size()).arg(rows.size() - succeeded));
            }
        }
        buffer.clear();
    }
}
