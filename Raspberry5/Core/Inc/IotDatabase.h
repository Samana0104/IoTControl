#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IotBluetooth.h"

typedef struct _BluetoothDeviceRecord
{
    char mac[BLUETOOTH_MAC_TEXT_SIZE];
} BluetoothDeviceRecord;

typedef struct st_mysql MYSQL;

int InitializeDatabase(void);
/* InitializeDatabase() must succeed first. Each opened connection must be closed
   on the same thread; these functions manage MariaDB thread initialization. */
MYSQL *OpenDatabaseConnection(void);
void CloseDatabaseConnection(MYSQL *connection);
int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength);
int GetMemberBluetoothDevice(const char *memberId, size_t memberIdLength, BluetoothDeviceRecord *deviceRecord);
int RegisterMemberBluetoothDevice(const char *memberId, size_t memberIdLength, const char *bluetoothMac, size_t bluetoothMacLength);
