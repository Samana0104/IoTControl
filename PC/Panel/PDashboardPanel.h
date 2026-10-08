#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace Ui
{
class DashboardWidget;
}
class FanChartWidget;
class FanRotorWidget;
class QLabel;
class QParallelAnimationGroup;
class QPushButton;
class QWidget;

class DashboardPanel final : public QObject
{
    Q_OBJECT

  public:
    explicit DashboardPanel(QWidget *host);
    ~DashboardPanel() override;
    int ReadTarget() const;
    void SetTarget(int percent);
    void DisplayTarget(int percent, int rotorSpeed);
    void SetServerEndpoint(const QString &host, int port);
    void SetFeedback(const QString &message);
    void SetClients(const QList<QStringList> &rows);
    void DisplayDhtClients(const QList<QStringList> &rows);
    void DisplayCurrentDht(const QString &temperature, const QString &humidity, const QString &source);
    void SetDhtLoading(bool loading, bool collecting = false);
    void ResetDhtLabels();
    void DisplayFanSpeed(int percent, bool sample = false);
    void DisplayFanSaved(int percent);
    void SetFanStatus(const QString &status, const QString &detail = QString());
    void SetFanUpdateMode(bool enabled);
    void SetFanUpdateBusy(bool busy);

  signals:
    void ReturnToLogin();
    void ServerChangeRequested();
    void ServerDisconnectRequested();
    void BluetoothRequested();
    void TargetChanged(int percent);
    void ApplyFanRequested();
    void ReloadDbRequested();
    void UpdateAllRequested();
    void ClientUpdateRequested(const QString &clientId);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void SelectSection(QPushButton *navigation);
    void PlayEntrance();
    void FinishEntrance();
    void SetMetricValue(QLabel *label, const QString &text);

    QWidget *host;
    Ui::DashboardWidget *ui;
    FanChartWidget *fanChart;
    FanRotorWidget *fanRotor;
    QPointer<QParallelAnimationGroup> entranceAnimation;
    QList<QWidget *> entrancePanels;
    QList<QLabel *> metricLabels;
    QStringList metricFinalText;
    int overviewTableMaximumHeight;
    int overviewChartMaximumHeight;
    bool fanUpdateMode = false;
};
