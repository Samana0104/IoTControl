#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

namespace Ui
{
class DashboardWidget;
}
class FanChartWidget;
class FanRotorWidget;
class QLabel;
class QParallelAnimationGroup;
class QPushButton;

class DashboardWidget final : public QWidget
{
    Q_OBJECT

  public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget() override;
    void SetServerPreview(const QString &host, int port);

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  signals:
    void ReturnToLogin();
    void ServerChangeRequested();

  private:
    void ShowBluetoothDialog();
    void SetTargetPreview(int percent);
    void ApplyFanPreview();
    void SelectSection(QPushButton *navigation);
    void PopulateSampleClients();
    void PlayEntrance();
    void FinishEntrance();

    Ui::DashboardWidget *ui;
    FanChartWidget *fanChart;
    FanRotorWidget *fanRotor;
    QPointer<QParallelAnimationGroup> entranceAnimation;
    QList<QWidget *> entrancePanels;
    QList<QLabel *> metricLabels;
    QStringList metricFinalText;
    int overviewTableMaximumHeight;
    int overviewChartMaximumHeight;
};
