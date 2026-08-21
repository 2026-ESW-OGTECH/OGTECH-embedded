#include "jetson_gate.h"

#define JETSON_GATE_GPIO_PORT   GPIOC
#define JETSON_GATE_GPIO_PIN    GPIO_PIN_9   /* Jetson 전원 MOSFET gate, active-high */

static uint8_t jetson_gate_on = 1u;

void JetsonGate_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* push-pull, 초기값 gate ON */
  HAL_GPIO_WritePin(JETSON_GATE_GPIO_PORT, JETSON_GATE_GPIO_PIN, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = JETSON_GATE_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(JETSON_GATE_GPIO_PORT, &GPIO_InitStruct);

  JetsonGate_Set(1u);
}

void JetsonGate_Set(uint8_t on)
{
  jetson_gate_on = on ? 1u : 0u;
  HAL_GPIO_WritePin(JETSON_GATE_GPIO_PORT, JETSON_GATE_GPIO_PIN,
                    jetson_gate_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t JetsonGate_IsOn(void)
{
  return jetson_gate_on;
}
