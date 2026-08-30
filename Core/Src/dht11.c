#include "dht11.h"

#include <string.h>

/* DWT CYCCNT가 돌지 않아도(잠금 해제 실패·디버거 상태) 스핀이 영구화되지 않도록
 * HAL tick으로 2차 상한을 둔다. DHT11 비트 하나는 ≤120 µs라 2 ms면 충분히 넉넉하다.
 * (2026-08-30, WORKLOG #4) */
#define DHT11_SPIN_LIMIT_MS 2u
#define DWT_LAR_UNLOCK_KEY  0xC5ACCE55u

static void delay_us(uint32_t us)
{
  uint32_t cycles_per_us = SystemCoreClock / 1000000u;
  uint32_t start = DWT->CYCCNT;
  uint32_t start_tick = HAL_GetTick();
  uint32_t wait_cycles = us * cycles_per_us;

  while ((uint32_t)(DWT->CYCCNT - start) < wait_cycles)
  {
    if ((uint32_t)(HAL_GetTick() - start_tick) > DHT11_SPIN_LIMIT_MS)
    {
      break;
    }
  }
}

static void DHT11_SetOutputOD(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = DHT11_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);
}

static void DHT11_SetInput(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = DHT11_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);
}

/* Wait until pin is NOT equal to 'state'. Returns 1 on success, 0 on timeout. */
static uint8_t DHT11_WaitWhile(GPIO_PinState state, uint32_t timeout_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t start_tick = HAL_GetTick();
  uint32_t timeout_cycles = timeout_us * (SystemCoreClock / 1000000u);

  while (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) == state)
  {
    if ((uint32_t)(DWT->CYCCNT - start) > timeout_cycles)
    {
      return 0u;
    }
    if ((uint32_t)(HAL_GetTick() - start_tick) > DHT11_SPIN_LIMIT_MS)
    {
      return 0u;
    }
  }

  return 1u;
}

void DHT11_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  /* Cortex-M7은 DWT 레지스터가 잠겨 있어 LAR에 키를 쓰지 않으면 CYCCNT가 돌지 않는다. */
  DWT->LAR = DWT_LAR_UNLOCK_KEY;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  DHT11_SetOutputOD();
  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
}

uint8_t DHT11_Read(DHT11_Data_t *out)
{
  uint8_t data[5] = {0, 0, 0, 0, 0};
  uint8_t i;

  if (out == NULL)
  {
    return 0u;
  }

  out->ok = 0u;

  /*
   * DHT11 start signal:
   * MCU pulls DATA low for >=18 ms, releases it, then reads 40 bits.
   */
  DHT11_SetOutputOD();
  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
  HAL_Delay(2);

  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);

  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
  delay_us(30);
  DHT11_SetInput();

  /* DHT11 response: ~80 us LOW, ~80 us HIGH */
  if (!DHT11_WaitWhile(GPIO_PIN_SET, 120u)) return 0u;
  if (!DHT11_WaitWhile(GPIO_PIN_RESET, 120u)) return 0u;
  if (!DHT11_WaitWhile(GPIO_PIN_SET, 120u)) return 0u;

  for (i = 0u; i < 40u; i++)
  {
    uint32_t high_start;
    uint32_t high_us;

    /* Each bit starts with ~50 us LOW. */
    if (!DHT11_WaitWhile(GPIO_PIN_RESET, 100u)) return 0u;

    /* Measure HIGH pulse. ~26-28 us = 0, ~70 us = 1. */
    high_start = DWT->CYCCNT;

    if (!DHT11_WaitWhile(GPIO_PIN_SET, 120u)) return 0u;

    high_us = (uint32_t)(DWT->CYCCNT - high_start) /
              (SystemCoreClock / 1000000u);

    data[i / 8u] <<= 1u;
    if (high_us > 50u)
    {
      data[i / 8u] |= 1u;
    }
  }

  DHT11_SetOutputOD();
  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);

  memcpy(out->raw, data, 5u);

  if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
  {
    return 0u;
  }

  out->hum_int  = data[0];
  out->hum_dec  = data[1];
  out->temp_int = data[2];
  out->temp_dec = data[3];
  out->ok = 1u;

  return 1u;
}
