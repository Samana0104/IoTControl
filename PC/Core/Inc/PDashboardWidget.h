#pragma once

#include "PDhtRecord.h"

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
    void SetDataFeedback(const QString &message);
    void SetDhtLoading(bool loading, bool collecting = false);
    void DisplayFanSpeed(int percent);
    void SetFanLoading();
    void SetFanError(const QString &message);
    void ResetDhtView();

  signals:
    void ReturnToLogin();
    void ServerChangeRequested();
    void ServerDisconnectRequested();
    void ReloadDhtRequested();
    void FieldDataUpdateRequested(const QString &clientId);

  private:
    void ShowBluetoothDialog();
    void SetTargetPreview(int percent);
    void ApplyFanPreview();
    void PopulateSampleClients();
    void RequestDhtReload();
    void RequestAllUpdate();
    void RequestClientUpdate(const QString &clientId);

    DashboardPanel *panel;
};
