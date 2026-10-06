#pragma once

#include "IoTPacket.h"

#include <stdint.h>

#define BLUETOOTH_MAC_TEXT_SIZE (BLUETOOTH_MAC_SIZE + 1)

int ConnectBluetoothDevice(const char *bluetoothMac, int timeoutMs, uint8_t *rfcommChannel);
int PairBluetoothDevice(const char *bluetoothMac, const char *pin, int timeoutSeconds);
void DisconnectBluetoothDevice(int bluetoothFd);
