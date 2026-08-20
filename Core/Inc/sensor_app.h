#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include "main.h"

/*
 * Application-level integration:
 *   USART1 -> Air530 GPS
 *   USART2 -> ZE16B-CO
 *   USART3 -> TeraTerm/debug output
 */
HAL_StatusTypeDef SensorApp_Init(UART_HandleTypeDef *gps_uart,
                                 UART_HandleTypeDef *co_uart,
                                 UART_HandleTypeDef *debug_uart);

/* Call continuously from while(1). */
void SensorApp_Process(void);

/* Call from the global HAL UART callbacks in main.c. */
void SensorApp_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void SensorApp_UART_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* SENSOR_APP_H */
