#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include "main.h"

/*
 * Application-level integration:
 *   USART1 -> Air530 GPS
 *   USART2 -> ZE16B-CO
 *   UART4  -> Jetson(GpsService) 링크 — JSONL+CRC16 telemetry + commands (PC10/PC11, 40핀 UART)
 *   USART3 -> 사람 콘솔 미러(TeraTerm, ST-LINK VCP) — 같은 출력, 같은 명령 수신. NULL 허용
 *   PB0    -> CO alarm buzzer
 *   PC9    -> Jetson power MOSFET gate
 *
 * 드라이버(air530_gps · dht11 · ze16b_co · co_alarm · jetson_gate · console)와
 * 순수 프로토콜(telemetry_protocol)을 묶어 2초 주기 텔레메트리, 명령 처리,
 * 경보·게이트·트레일 watchdog을 돌린다.
 *
 * 2026-08-30 통합: 종전에는 USART3 하나가 Jetson 링크였다(ST-LINK VCP /dev/ttyACM0).
 * 실기 배선은 UART4 ↔ Jetson 40핀(/dev/ttyTHS0)이라 링크를 UART4로 옮기고 USART3를 미러로 남겼다.
 * mirror_uart에 link_uart와 같은 핸들이나 NULL을 주면 종전과 동일하게 링크 하나로만 동작한다.
 */
HAL_StatusTypeDef SensorApp_Init(UART_HandleTypeDef *gps_uart,
                                 UART_HandleTypeDef *co_uart,
                                 UART_HandleTypeDef *link_uart,
                                 UART_HandleTypeDef *mirror_uart);

/* Call continuously from while(1). */
void SensorApp_Process(void);

/* Call from the global HAL UART callbacks in main.c. */
void SensorApp_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void SensorApp_UART_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* SENSOR_APP_H */
