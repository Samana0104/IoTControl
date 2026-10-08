#pragma once

#include <QByteArray>
#include <QMainWindow>

class AccessPanel;
class DashboardWidget;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void ShowDashboard();

  signals:
    void LoginRequested(const QByteArray &packet);

  private:
    void InitializeServerControls();
    void InitializeLoginControls();
    void ShowServerConnection();
    void ShowLogin();
    void ConfirmServerPreview();
    void SubmitLogin();

    AccessPanel *accessPanel;
    QStackedWidget *pages;
    DashboardWidget *dashboard;
    QSize loginWindowSize;
    bool serverPreviewReady = false;
};
