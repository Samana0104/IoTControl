#pragma once

#include "IoTPacket.h"

#include <stddef.h>
#include <stdint.h>

#define BLUETOOTH_MAC_TEXT_SIZE (BLUETOOTH_MAC_SIZE + 1)
#define BLUETOOTH_NAME_TEXT_SIZE 64

// 검색으로 찾은 장치 하나
typedef struct _BluetoothScanDevice
{
    char mac[BLUETOOTH_MAC_TEXT_SIZE];
    char name[BLUETOOTH_NAME_TEXT_SIZE]; // 이름을 아직 못 받았으면 빈 문자열 (길면 잘림)
    int16_t rssi;
    int paired;
} BluetoothScanDevice;

int ConnectBluetoothDevice(const char *bluetoothMac, int timeoutMs, uint8_t *rfcommChannel);
int PairBluetoothDevice(const char *bluetoothMac, const char *pin, int timeoutSeconds);
void DisconnectBluetoothDevice(int bluetoothFd);
// 클래식 BT(HC-05)를 scanSeconds 동안 검색해 이번 검색에서 보인(RSSI 있는) 장치를 devices에 채움
// nameFilter가 NULL이나 빈 문자열이 아니면 이름에 그 문자열이 들어간 장치만 (대소문자 무시)
// 찾은 장치 수(최대 maxDevices), 실패하면 -1 (errno 설정)
int ScanBluetoothDevices(const char *nameFilter, int scanSeconds, BluetoothScanDevice *devices, size_t maxDevices);
