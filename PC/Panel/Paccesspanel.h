#pragma once

#include <QObject>
#include <QString>

namespace Ui
{
class MainWindow;
}
class QMainWindow;
class QVariantAnimation;
class QWidget;

// 서버/로그인 화면의 표시와 입력 이벤트만 담당합니다.
class AccessPanel final : public QObject
{
    Q_OBJECT

  public:
    explicit AccessPanel(QMainWindow *window);
    ~AccessPanel() override;
    QString ReadServerHost() const;
    QString ReadServerPort() const;
    QString ReadUsername() const;
    QString ReadPassword() const;
    bool ReadRememberUsername() const;
    bool IsAccessVisible() const;
    void RestoreUsername(bool remember, const QString &username);
    void SetServerSummary(const QString &host, int port);
    void SetLoginEnabled(bool enabled);
    void SetServerConnecting(bool connecting);
    void ResetPassword();
    void ClearServerFeedback();
    void ShowServerError(const QString &message, bool invalidHost);
    void SetServerFeedback(const QString &message);
    void ClearLoginFeedback();
    void ShowLoginError(const QString &message, bool invalidId,
                        bool invalidPassword);
    void SetLoginFeedback(const QString &message, bool error);
    void ShowServerPage(bool animate);
    void ShowLoginPage(bool animate);
    void FinishAccessTransition();

  signals:
    void ServerConnectRequested();
    void ServerChangeRequested();
    void ServerDisconnectRequested();
    void LoginSubmitted();
    void DashboardPreviewRequested();
    void RememberUsernameChanged(bool remember);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void InitializeDesign();
    void InitializeInputEvents();
    void InitializeAccessTransition();
    void ShowAccessPage(QWidget *page, bool animate);
    void FocusAccessPage();

    QMainWindow *window;
    Ui::MainWindow *ui;
    QWidget *accessRoot;
    QWidget *accessTransition;
    QVariantAnimation *accessAnimation;
};
