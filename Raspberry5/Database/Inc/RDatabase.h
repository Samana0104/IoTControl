#pragma once

#include <stddef.h>
#include <stdint.h>

#include "RBluetooth.h"
#include "IoTPacket.h"

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
/* UPDATE only; rows must already exist. Returns 0 on success, -1 on error.
   affectedRows == 0 means either unchanged values or no matching row. */
int UpdateDhtData(const char *memberId, size_t memberIdLength, const DhtData *data, uint64_t *affectedRows);
int UpdateFanData(const FanData *data, uint64_t *affectedRows);
int UpdateConData(const ConData *data, uint64_t *affectedRows);
int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength);
int GetMemberBluetoothDevice(const char *memberId, size_t memberIdLength, BluetoothDeviceRecord *deviceRecord);
int RegisterMemberBluetoothDevice(const char *memberId, size_t memberIdLength, const char *bluetoothMac, size_t bluetoothMacLength);
