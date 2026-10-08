#pragma once
#include <QDialog>
class ServerBluetooth;
class BluetoothManagerPanel;
class BluetoothManagerDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit BluetoothManagerDialog(ServerBluetooth *query, QWidget *parent = nullptr);
    ~BluetoothManagerDialog() override;

  private:
    void Refresh();
    ServerBluetooth *query;
    BluetoothManagerPanel *panel;
};
