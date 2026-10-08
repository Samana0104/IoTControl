#pragma once

#include "PDhtRecord.h"
#include "PSessionRecord.h"

#include <QString>
#include <QWidget>

class DashboardPanel;

class DashboardWidget final : public QWidget
{
    Q_OBJECT

  public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget() override;
    void SetServerEndpoint(const QString &host, int port);
    void DisplayDhtRecords(const DhtRecords &records, bool showFeedback = true);
    void DisplaySessionRecords(const SessionRecords &records);
    void SetSessionError(const QString &message);
    void ResetSessionStatus();
    void SetDataFeedback(const QString &message);
    void SetClientRefreshing(const QString &clientId);
    void SetDhtLoading(bool loading, bool collecting = false);
    void DisplayFanSpeed(int percent);
    void DisplayFanSaved(int percent);
    QString ReadFanClientId() const;
    void DisplayFanApplied(const QString &clientId, int percent);
    void SetFanApplyError(const QString &message);
    void SetFanLoading();
    void SetFanError(const QString &message);
    void SetFanUpdateMode(bool enabled);
    void SetFanUpdateBusy(bool busy, const QString &stage = QString());
    void ResetDhtView();

  signals:
    void BluetoothManageRequested();
    void ReturnToLogin();
    void ServerChangeRequested();
    void ServerDisconnectRequested();
    void ReloadDhtRequested();
    void FieldDataUpdateRequested(const QString &clientId);
    void FanUpdateRequested(int percent);

  private:
    void ShowBluetoothDialog();
    void SetTargetPreview(int percent);
    void ApplyFanPreview();
    void PopulateSampleClients();
    void RequestDhtReload();
    void RequestAllUpdate();
    void RequestClientUpdate(const QString &clientId);
    void DisplayClientRecords();

    DashboardPanel *panel;
    bool fanUpdateMode = false;
    DhtRecords dhtRecords;
    SessionRecords sessionRecords;
    bool sessionStatusKnown = false;
    QString sessionError;
};
