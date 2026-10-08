#pragma once

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

  signals:
    void ReturnToLogin();
    void ServerChangeRequested();
    void ServerDisconnectRequested();

  private:
    void ShowBluetoothDialog();
    void SetTargetPreview(int percent);
    void ApplyFanPreview();
    void PopulateSampleClients();
    void ReloadDbPreview();
    void RequestAllPreview();
    void RequestClientPreview(const QString &clientId);

    DashboardPanel *panel;
};
