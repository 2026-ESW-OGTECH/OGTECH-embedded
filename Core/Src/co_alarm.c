#include "co_alarm.h"

#define CO_WARN_PPM        35u          /* 주의: 35 ppm 지속 */
#define CO_WARN_HOLD_MS    180000u      /* 3분 지속 시 WARN */
#define CO_ALARM_PPM       100u         /* 100 ppm 즉시 ALARM (예열 중에도) */
#define CO_CLEAR_PPM       30u          /* 30 ppm 미만 */
#define CO_CLEAR_HOLD_MS   30000u       /* 30초 지속 시 해제 */

static CoAlarmState_t co_alarm_state = CO_ALARM_NONE;
static uint32_t co_warn_since_ms = 0u;
static uint8_t  co_warn_tracking = 0u;
static uint32_t co_clear_since_ms = 0u;
static uint8_t  co_clear_tracking = 0u;
static uint32_t alarm_boot_ms = 0u;

void CoAlarm_Init(uint32_t boot_ms)
{
  co_alarm_state = CO_ALARM_NONE;
  co_warn_tracking = 0u;
  co_clear_tracking = 0u;
  alarm_boot_ms = boot_ms;
}

uint8_t CoAlarm_IsWarmingUp(uint32_t now)
{
  return ((uint32_t)(now - alarm_boot_ms) <= CO_ALARM_WARMUP_MS) ? 1u : 0u;
}

uint8_t CoAlarm_IsFresh(uint32_t now, const ZE16BCO_Data_t *co)
{
  if (co == NULL)
  {
    return 0u;
  }
  return (co->valid &&
          ((uint32_t)(now - co->last_valid_ms) <= CO_ALARM_FRESH_MS)) ? 1u : 0u;
}

void CoAlarm_Update(uint32_t now, const ZE16BCO_Data_t *co)
{
  uint8_t fresh = CoAlarm_IsFresh(now, co);

  /* 센서 입력이 끊겼다는 이유만으로는 이미 발생한 경보를 해제하지 않는다. */
  if (!fresh)
  {
    co_warn_tracking = 0u;
    co_clear_tracking = 0u;
    return;
  }

  /* 100 ppm 즉시 경보 - 예열 중에도 안전 편향으로 적용한다. */
  if (co->ppm >= CO_ALARM_PPM)
  {
    co_alarm_state = CO_ALARM_ALARM;
    co_warn_tracking = 0u;
    co_clear_tracking = 0u;
    return;
  }

  if (CoAlarm_IsWarmingUp(now))
  {
    return; /* 예열 중 저농도 값은 판정에 쓰지 않는다. */
  }

  if (co->ppm >= CO_WARN_PPM)
  {
    co_clear_tracking = 0u;
    if (!co_warn_tracking)
    {
      co_warn_tracking = 1u;
      co_warn_since_ms = now;
    }
    if ((co_alarm_state == CO_ALARM_NONE) &&
        ((uint32_t)(now - co_warn_since_ms) >= CO_WARN_HOLD_MS))
    {
      co_alarm_state = CO_ALARM_WARN;
    }
    return;
  }

  co_warn_tracking = 0u;

  if ((co_alarm_state != CO_ALARM_NONE) && (co->ppm < CO_CLEAR_PPM))
  {
    if (!co_clear_tracking)
    {
      co_clear_tracking = 1u;
      co_clear_since_ms = now;
    }
    if ((uint32_t)(now - co_clear_since_ms) >= CO_CLEAR_HOLD_MS)
    {
      co_alarm_state = CO_ALARM_NONE;
      co_clear_tracking = 0u;
    }
  }
  else
  {
    co_clear_tracking = 0u;
  }
}

CoAlarmState_t CoAlarm_GetState(void)
{
  return co_alarm_state;
}
