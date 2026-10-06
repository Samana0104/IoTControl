#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IotBluetooth.h"

typedef struct _BluetoothDeviceRecord
{
    char mac[BLUETOOTH_MAC_TEXT_SIZE];
} BluetoothDeviceRecord;

int InitializeDatabase(void);
int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength);
int GetMemberBluetoothDevice(const char *memberId, size_t memberIdLength, BluetoothDeviceRecord *deviceRecord);
int RegisterMemberBluetoothDevice(const char *memberId, size_t memberIdLength, const char *bluetoothMac, size_t bluetoothMacLength);
