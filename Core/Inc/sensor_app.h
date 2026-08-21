#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include "main.h"

/*
 * Application-level integration:
 *   USART1 -> Air530 GPS
 *   USART2 -> ZE16B-CO
 *   USART3 -> Jetson(GpsService) / TeraTerm  — JSONL+CRC16 telemetry + commands
 *   PB0    -> CO alarm buzzer
 *   PC9    -> Jetson power MOSFET gate
 *
 * 드라이버(air530_gps · dht11 · ze16b_co · co_alarm · jetson_gate · console)와
 * 순수 프로토콜(telemetry_protocol)을 묶어 2초 주기 텔레메트리, 명령 처리,
 * 경보·게이트·트레일 watchdog을 돌린다.
 */
HAL_StatusTypeDef SensorApp_Init(UART_HandleTypeDef *gps_uart,
                                 UART_HandleTypeDef *co_uart,
                                 UART_HandleTypeDef *console_uart);

/* Call continuously from while(1). */
void SensorApp_Process(void);

/* Call from the global HAL UART callbacks in main.c. */
void SensorApp_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void SensorApp_UART_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* SENSOR_APP_H */
