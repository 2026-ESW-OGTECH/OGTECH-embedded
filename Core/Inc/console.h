#ifndef CONSOLE_H
#define CONSOLE_H

#include "main.h"
#include <stddef.h>
#include <stdint.h>

/*
 * USART3 전송 계층 — Jetson(GpsService) 또는 TeraTerm이 상대다.
 * 수신: 1바이트 인터럽트 + 링 버퍼 → 개행(\r 또는 \n) 종결 한 줄.
 * 과길이 줄(CONSOLE_LINE_SIZE-1 초과)은 개행까지 통째로 버리고
 * ERR LINE_TOO_LONG 으로 답한다(꼬리가 명령으로 해석되는 경로 차단).
 * 명령 해석은 하지 않는다 — sensor_app이 telemetry_protocol의 파서로 처리한다.
 */

#define CONSOLE_LINE_SIZE 32u

HAL_StatusTypeDef Console_Init(UART_HandleTypeDef *huart);

/* 블로킹 송신. text는 NUL 종결 ASCII. */
void Console_Print(const char *text);

/* 완성된 한 줄이 있으면 out에 복사(NUL 종결)하고 1, 없으면 0. 빈 줄은 건너뛴다. */
uint8_t Console_ReadLine(char *out, size_t cap);

/* HAL UART 콜백 전달용. */
void Console_RxCpltCallback(UART_HandleTypeDef *huart);
void Console_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* CONSOLE_H */
