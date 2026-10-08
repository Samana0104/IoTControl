#pragma once

#include <QObject>
#include <QString>

namespace Ui
{
class BluetoothDialog;
}
class QDialog;

class BluetoothPanel final : public QObject
{
    Q_OBJECT

  public:
    explicit BluetoothPanel(QDialog *dialog);
    ~BluetoothPanel() override;
    QString ReadId() const;
    QString ReadPassword() const;
    void ClearPassword();
    void ClearFeedback();
    void ShowInputError(const QString &message, bool invalidId);

  signals:
    void RequestSubmitted();
    void CancelRequested();

  private:
    Ui::BluetoothDialog *ui;
};
