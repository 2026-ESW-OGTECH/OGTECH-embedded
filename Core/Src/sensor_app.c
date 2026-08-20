#include "sensor_app.h"

#include "air530_gps.h"
#include "dht11.h"
#include "ze16b_co.h"

#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef *debug_uart_handle = NULL;

static DHT11_Data_t dht11;

static uint32_t boot_ms = 0u;
static uint32_t last_status_ms = 0u;

static void Debug_Print(const char *text)
{
  if ((debug_uart_handle == NULL) || (text == NULL))
  {
    return;
  }

  (void)HAL_UART_Transmit(debug_uart_handle,
                          (uint8_t *)text,
                          (uint16_t)strlen(text),
                          HAL_MAX_DELAY);
}

static void Status_Print(void)
{
  const Air530_Data_t *gps = Air530_GetData();
  const ZE16BCO_Data_t *co = ZE16BCO_GetData();

  char line[256];
  char lat[24];
  char lon[24];
  uint32_t now = HAL_GetTick();
  uint32_t elapsed_ms = now - boot_ms;
  uint32_t remaining_s;
  int n = 0;

  /* DHT11 */
  if (dht11.ok)
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "DHT11=OK,TEMP=%u.%uC,HUM=%u.%u%%,",
                  (unsigned)dht11.temp_int,
                  (unsigned)dht11.temp_dec,
                  (unsigned)dht11.hum_int,
                  (unsigned)dht11.hum_dec);
  }
  else
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "DHT11=ERROR,");
  }

  /* ZE16B-CO: preserve original 30 s warm-up handling. */
  if (elapsed_ms <= 30000u)
  {
    remaining_s = (30000u - elapsed_ms + 999u) / 1000u;
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "CO=WARMING_UP(%lus),",
                  (unsigned long)remaining_s);
  }
  else if (co->valid && ((uint32_t)(now - co->last_valid_ms) <= 3000u))
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "CO=%uppm,",
                  (unsigned)co->ppm);
  }
  else
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "CO=NOT_FOUND,");
  }

  /* GPS */
  if ((!gps->nmea_seen) ||
      ((uint32_t)(now - gps->last_nmea_ms) > 5000u))
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "GPS=NOT_FOUND");
  }
  else if (!gps->fix)
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "GPS=NO_FIX,SAT=%u",
                  (unsigned)gps->satellites);
  }
  else
  {
    Air530_FormatE7(gps->lat_e7, lat, sizeof(lat));
    Air530_FormatE7(gps->lon_e7, lon, sizeof(lon));

    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "GPS=FIX,LAT=%s,LON=%s,SAT=%u",
                  lat,
                  lon,
                  (unsigned)gps->satellites);
  }

  (void)snprintf(&line[n], sizeof(line) - (size_t)n, "\r\n");
  Debug_Print(line);
}

HAL_StatusTypeDef SensorApp_Init(UART_HandleTypeDef *gps_uart,
                                 UART_HandleTypeDef *co_uart,
                                 UART_HandleTypeDef *debug_uart)
{
  HAL_StatusTypeDef status;

  if ((gps_uart == NULL) || (co_uart == NULL) || (debug_uart == NULL))
  {
    return HAL_ERROR;
  }

  debug_uart_handle = debug_uart;

  DHT11_Init();

  status = Air530_Init(gps_uart);
  if (status != HAL_OK)
  {
    return status;
  }

  status = ZE16BCO_Init(co_uart);
  if (status != HAL_OK)
  {
    return status;
  }

  boot_ms = HAL_GetTick();
  last_status_ms = boot_ms - 2000u;

  Debug_Print("\r\n=== SURVIVAL SENSOR START ===\r\n");
  Debug_Print("USART1=Air530 GPS 9600, USART2=ZE16B-CO 9600, USART3=TeraTerm 115200\r\n");

  return HAL_OK;
}

void SensorApp_Process(void)
{
  uint32_t now;

  Air530_Process();
  ZE16BCO_Process();

  now = HAL_GetTick();

  /*
   * DHT11 should not be sampled too frequently.
   * Read + print once every 2 seconds.
   */
  if ((uint32_t)(now - last_status_ms) >= 2000u)
  {
    last_status_ms = now;

    (void)DHT11_Read(&dht11);

    /* Drain UART bytes received by IRQ while DHT11 was being read. */
    Air530_Process();
    ZE16BCO_Process();

    Status_Print();
  }
}

void SensorApp_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  Air530_RxCpltCallback(huart);
  ZE16BCO_RxCpltCallback(huart);
}

void SensorApp_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  Air530_ErrorCallback(huart);
  ZE16BCO_ErrorCallback(huart);
}
