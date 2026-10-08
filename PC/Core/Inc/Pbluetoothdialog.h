#pragma once

#include <QDialog>

class BluetoothPanel;

class BluetoothDialog final : public QDialog
{
    Q_OBJECT

  public:
    explicit BluetoothDialog(QWidget *parent = nullptr);
    ~BluetoothDialog() override;

  signals:
    void PreviewRequested(const QString &id, const QString &password);

  private:
    void ValidateRequest();
    BluetoothPanel *panel;
};
