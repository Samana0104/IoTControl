#pragma once

#include "PDhtRecord.h"
#include "PLoginResult.h"

#include <QByteArray>
#include <QMainWindow>
#include <QSize>
#include <QString>
#include <QtTypes>

typedef struct _MemData MemData;

class AccessPanel;
class DashboardWidget;
class QStackedWidget;
class QTimer;
class ServerConnection;
class ServerLogin;
class ServerDhtQuery;
class ServerFanQuery;

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
    void SubmitLogin();
    bool ReadLoginMember(MemData &member);
    void SaveLoginUsername();
    void HandleLoginResult(LoginResult result);
    void HandleLoginSuccess();
    void InitializeDhtControls();
    void LoadAllDht(bool background = false);
    void HandleDhtLoaded(const DhtRecords &records);
    void HandleDhtQueryFailed(const QString &message);
    void RequestFieldDataUpdate(const QString &clientId);
    void HandleFieldDataUpdateResult(bool requested);
    void InitializeFanControls();
    void LoadFanSpeed();
    void HandleFanLoaded(int percent);
    void HandleFanQueryFailed(const QString &message);

    AccessPanel *accessPanel;
    QStackedWidget *pages;
    DashboardWidget *dashboard;
    QSize loginWindowSize;
    ServerConnection *serverConnection;
    ServerLogin *serverLogin;
    ServerDhtQuery *serverDhtQuery;
    ServerFanQuery *serverFanQuery;
    QTimer *dhtPollTimer;
    bool authenticated = false;
    bool backgroundDhtQuery = false;
    QString serverHost;
    quint16 serverPort = 0;
};
