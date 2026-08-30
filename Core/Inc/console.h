#ifndef CONSOLE_H
#define CONSOLE_H

#include "main.h"
#include <stddef.h>
#include <stdint.h>

/*
 * 콘솔 전송 계층 — 링크(Jetson, UART4)와 선택적 미러(사람 콘솔, USART3).
 *   송신: 링크와 미러 양쪽에 같은 줄을 블로킹 송신한다(미러가 NULL이면 링크만).
 *   수신: 두 UART 모두 1바이트 인터럽트 → 공용 링 버퍼(256 B) → 개행(\r 또는 \n) 종결 한 줄.
 *         텔레메트리 한 줄(≈330자, 115200에서 ≈29 ms)을 블로킹 송신하는 동안 Jetson 명령이
 *         들어와도 링이 받아 둔다(2026-08-30, WORKLOG #10: 32 B 링에서 유실 가능했던 경로).
 * 과길이 줄(CONSOLE_LINE_SIZE-1 초과)은 개행까지 통째로 버리고 ERR LINE_TOO_LONG 으로 답한다
 * (꼬리가 명령으로 해석되는 경로 차단). 명령 해석은 하지 않는다 — sensor_app이 telemetry_protocol의
 * 파서로 처리한다.
 */

#define CONSOLE_LINE_SIZE 64u

/* link: 필수(Jetson 프로토콜 링크). mirror: NULL 허용(TeraTerm 등 사람 콘솔). */
HAL_StatusTypeDef Console_Init(UART_HandleTypeDef *link, UART_HandleTypeDef *mirror);

/* 블로킹 송신. text는 NUL 종결 ASCII. 링크 → 미러 순서로 보낸다. */
void Console_Print(const char *text);

/* 완성된 한 줄이 있으면 out에 복사(NUL 종결)하고 1, 없으면 0. 빈 줄은 건너뛴다. */
uint8_t Console_ReadLine(char *out, size_t cap);

/* HAL UART 콜백 전달용. 링크·미러 어느 쪽 UART든 여기로 온다. */
void Console_RxCpltCallback(UART_HandleTypeDef *huart);
void Console_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* CONSOLE_H */
