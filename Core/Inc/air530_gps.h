#ifndef AIR530_GPS_H
#define AIR530_GPS_H

#include "main.h"
#include <stdint.h>

typedef struct
{
  uint8_t  nmea_seen;
  uint8_t  fix;
  uint8_t  satellites;
  int32_t  lat_e7;
  int32_t  lon_e7;
  uint32_t last_nmea_ms;
  uint8_t  ever_fix;      /* 부팅 후 한 번이라도 fix를 받았는지 (텔레메트리 last_age_s용) */
  uint32_t last_fix_ms;   /* 마지막 fix 좌표 갱신 시각 */
} Air530_Data_t;

/* Starts continuous 1-byte interrupt reception. */
HAL_StatusTypeDef Air530_Init(UART_HandleTypeDef *huart);

/* Consumes buffered bytes and parses NMEA GGA sentences. Call frequently. */
void Air530_Process(void);

/* Forward HAL UART callbacks to these functions. */
void Air530_RxCpltCallback(UART_HandleTypeDef *huart);
void Air530_ErrorCallback(UART_HandleTypeDef *huart);

/* Returns the current parsed GPS state. */
const Air530_Data_t *Air530_GetData(void);

/* Formats signed decimal degrees x 1e7 as a printable decimal string. */
void Air530_FormatE7(int32_t value, char *out, uint16_t out_size);

#endif /* AIR530_GPS_H */
