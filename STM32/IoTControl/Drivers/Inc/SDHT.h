#pragma once

#include <main.h>

// DHT11 온습도 센서 (보드에 1개, 상태는 드라이버 내부에서 관리)
// 신호선 1개 = 타이머 입력 캡처 핀 (CubeMX: TIMx_CHy Input Capture, 1MHz, ARR 65535, 하강 엣지,
//                                    TIMx global interrupt, 핀 Pull-up)
//   - 시작 신호: 핀을 잠깐 GPIO 출력 LOW로 바꿈 (MCU -> DHT)
//   - 수신: 핀을 타이머 입력으로 되돌려 하강 엣지 시각을 캡처 (DHT -> MCU)
//
// 읽기 흐름 (전부 타이머 인터럽트에서 진행, 메인 루프 호출/HAL_Delay 없음)
//   SDhtStartRead -> 20ms 뒤 업데이트 인터럽트: 핀을 타이머 입력으로 되돌리고 캡처 시작
//   -> 엣지 42개 모이면 캡처 인터럽트에서 해석 / 10ms 안에 못 모으면 업데이트 인터럽트에서 타임아웃
// 끝나면 SDhtIsBusy()가 false, 결과는 SDhtGetStatus / getter
// 이 드라이버가 HAL_TIM_PeriodElapsedCallback, HAL_TIM_IC_CaptureCallback을 정의함

typedef enum _SDhtStatus
{
    SDHT_STATUS_NONE,       // 아직 읽은 적 없음
    SDHT_STATUS_OK,
    SDHT_STATUS_TIMEOUT,    // 응답 엣지가 모자람 (배선/풀업/전원 확인)
    SDHT_STATUS_CHECKSUM,   // 받긴 했는데 데이터가 깨짐
} SDhtStatus;

// 타이머/채널 + 그 채널의 핀 연결
bool SDhtInit(TIM_HandleTypeDef *htim, uint32_t channel, GPIO_TypeDef *port, uint16_t pin);

// 읽기 시작, 진행 중이거나 최소 간격(1초)이 안 지났으면 false
bool SDhtStartRead(void);

bool SDhtIsBusy(void);
SDhtStatus SDhtGetStatus(void);

// 한 번이라도 읽기에 성공했는지 (false면 아래 값은 0)
bool SDhtHasData(void);

// 마지막으로 성공한 값, 0.1 단위 (예: 235 = 23.5도, 450 = 45.0%)
int16_t SDhtGetTemperature(void);
uint16_t SDhtGetHumidity(void);
