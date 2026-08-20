#ifndef ZE16B_CO_H
#define ZE16B_CO_H

#include "main.h"
#include <stdint.h>

typedef struct
{
  uint8_t  valid;
  uint16_t ppm;
  uint32_t last_valid_ms;
} ZE16BCO_Data_t;

/* Starts continuous 1-byte interrupt reception. */
HAL_StatusTypeDef ZE16BCO_Init(UART_HandleTypeDef *huart);

/* Consumes buffered bytes and parses 9-byte ZE16B-CO frames. */
void ZE16BCO_Process(void);

/* Forward HAL UART callbacks to these functions. */
void ZE16BCO_RxCpltCallback(UART_HandleTypeDef *huart);
void ZE16BCO_ErrorCallback(UART_HandleTypeDef *huart);

/* Returns the latest parsed CO state. */
const ZE16BCO_Data_t *ZE16BCO_GetData(void);

#endif /* ZE16B_CO_H */
