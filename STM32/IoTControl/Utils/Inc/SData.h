#pragma once

#include <main.h>
#include <stdbool.h>

// 전원이 꺼져도 유지되는 데이터 저장 (아두이노 AData의 STM32 버전, 내장 플래시 섹터 7 = 128KB)
//
// 슬롯 = [magic 1바이트][data length바이트]
//   magic이 SDATA_MAGIC이면 저장된 데이터 있음
//   슬롯 주소/크기는 SDefine.h의 SDATA_ADDR_* 참고, 슬롯끼리 겹치지 않게 배치
//
// 플래시는 섹터 단위로 지워야 다시 쓸 수 있어서 EEPROM처럼 바이트만 고칠 수 없음
//   전체 데이터(SDATA_SIZE)를 RAM에 두고, 저장할 때마다 섹터의 다음 빈 블록에 통째로 이어 씀
//   섹터가 꽉 차면(약 127번 저장) 그때만 섹터를 지우고 처음 블록부터 씀
//
// 주의
//   - 평문으로 저장됨 (보드에 물리 접근하면 읽을 수 있음)
//   - 섹터 지우는 동안(1~2초) CPU가 멈춤 (UART 수신은 DMA라 버퍼 크기만큼은 유지됨)
//   - 섹터 지우는 중 전원이 꺼지면 저장된 데이터 전체가 사라짐
//   - 링커 스크립트에서 섹터 7(0x08060000~)은 코드 영역에서 뺐음

#define SDATA_SIZE 1024

// 섹터에서 마지막으로 저장된 블록을 RAM으로 읽음, AppInit에서 한 번 호출
bool SDataInit(void);

// addr 슬롯에 data를 length만큼 저장, 범위를 넘거나 플래시 쓰기 실패하면 false
bool SDataSave(uint16_t addr, const uint8_t *data, uint16_t length);

// addr 슬롯에서 length만큼 읽음, 저장된 적 없거나 범위를 넘으면 false
bool SDataLoad(uint16_t addr, uint8_t *data, uint16_t length);

// addr 슬롯을 지움 (magic 포함 length + 1바이트), 플래시 쓰기 실패하면 false
bool SDataClear(uint16_t addr, uint16_t length);
