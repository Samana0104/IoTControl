#pragma once

#include <Arduino.h>

// 전원이 꺼져도 유지되는 데이터 저장 (Uno 내장 EEPROM 1KB)
//
// 슬롯 = [magic 1바이트][data length바이트]
//   magic이 DATA_MAGIC이면 저장된 데이터 있음
//   슬롯 주소/크기는 ADefine.h의 DATA_ADDR_* 참고, 슬롯끼리 겹치지 않게 배치
//
// 주의: 평문으로 저장됨 (보드에 물리 접근하면 읽을 수 있음)

// addr 슬롯에 data를 length만큼 저장, EEPROM 범위를 넘으면 false
bool SaveData(uint16_t addr, const uint8_t *data, size_t length);

// addr 슬롯에서 length만큼 읽음, 저장된 적 없거나 범위를 넘으면 false
bool LoadData(uint16_t addr, uint8_t *data, size_t length);

// addr 슬롯을 지움 (magic 포함 length + 1바이트)
void ClearData(uint16_t addr, size_t length);
