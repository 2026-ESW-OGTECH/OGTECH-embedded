#ifndef JETSON_GATE_H
#define JETSON_GATE_H

#include "main.h"
#include <stdint.h>

/*
 * Jetson 전원 MOSFET gate (PC9, active-high).
 * 부팅 시 ON. STM32가 리셋되면 Jetson 전원은 항상 켜진 상태에서 시작한다.
 */

void JetsonGate_Init(void);
void JetsonGate_Set(uint8_t on);
uint8_t JetsonGate_IsOn(void);

#endif /* JETSON_GATE_H */
