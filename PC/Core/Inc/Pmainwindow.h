#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QSize>
#include <QString>
#include <QtTypes>

class AccessPanel;
class DashboardWidget;
class QStackedWidget;
class ServerConnection;

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void ShowDashboard();

  signals:
    void LoginRequested(const QByteArray &packet);
    void ServerDataReceived(const QByteArray &data);

  private:
    void InitializeServerControls();
    void InitializeLoginControls();
    void ShowServerConnection();
    void ShowLogin();
    void ConnectToServer();
    void DisconnectFromServer();
    void HandleServerConnected();
    void HandleServerDisconnected();
    void HandleServerConnectionFailed(const QString &message);
    void SendLoginPacket(const QByteArray &packet);
    void SubmitLogin();

    AccessPanel *accessPanel;
    QStackedWidget *pages;
    DashboardWidget *dashboard;
    QSize loginWindowSize;
    ServerConnection *serverConnection;
    QString serverHost;
    quint16 serverPort = 0;
};
