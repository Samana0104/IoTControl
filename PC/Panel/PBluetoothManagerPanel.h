#pragma once
#include <QList>
#include <QObject>
#include <QStringList>
class QDialog;
namespace Ui
{
class BluetoothManagerDialog;
}
class BluetoothManagerPanel final : public QObject
{
    Q_OBJECT
  public:
    explicit BluetoothManagerPanel(QDialog *dialog);
    ~BluetoothManagerPanel() override;
    void Display(bool scanning, bool busy, const QList<QStringList> &rows, const QString &feedback);
  signals:
    void ScanRequested();
    void ConnectAllRequested();
    void CloseRequested();

  private:
    Ui::BluetoothManagerDialog *ui;
};
