#ifndef CO_ALARM_H
#define CO_ALARM_H

#include "main.h"
#include "ze16b_co.h"
#include <stdint.h>

/*
 * CO 경보 판정. 소리는 내지 않는다 — 경보음은 Jetson 스피커가 낸다(2026-08-31).
 * 35 ppm 3분 지속 WARN · 100 ppm 즉시 ALARM(예열 중에도) · 30 ppm 미만 30초 해제.
 * 센서 입력이 끊겨도 이미 올라간 경보는 내리지 않는다(latched).
 * 판정은 Jetson 전원과 무관하게 STM32 단독으로 계속 돌지만, Jetson 전원이 꺼져 있으면
 * 소리는 나지 않는다(종전 부저 PB0 출력 제거).
 */

typedef enum
{
  CO_ALARM_NONE = 0u,
  CO_ALARM_WARN,
  CO_ALARM_ALARM
} CoAlarmState_t;

#define CO_ALARM_WARMUP_MS   30000u   /* ZE16B-CO 제조사 규정 예열 */
#define CO_ALARM_FRESH_MS     3000u   /* 유효 프레임 신선 판정 */

/* 상태 초기화. boot_ms는 예열 기준 시각. */
void CoAlarm_Init(uint32_t boot_ms);

/* 매 루프 호출: 경보 상태만 갱신한다(논블로킹, 물리 출력 없음). */
void CoAlarm_Update(uint32_t now, const ZE16BCO_Data_t *co);

CoAlarmState_t CoAlarm_GetState(void);
uint8_t CoAlarm_IsWarmingUp(uint32_t now);
uint8_t CoAlarm_IsFresh(uint32_t now, const ZE16BCO_Data_t *co);

#endif /* CO_ALARM_H */
