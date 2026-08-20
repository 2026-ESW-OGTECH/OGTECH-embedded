/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : DHT11 + ZE16B-CO + Air530 GPS + TeraTerm status output
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

/* ---------- UART RX ring buffers ---------- */
#define GPS_RING_SIZE  256u
#define CO_RING_SIZE    64u

static uint8_t gps_rx_byte;
static uint8_t co_rx_byte;

static volatile uint8_t  gps_ring[GPS_RING_SIZE];
static volatile uint16_t gps_head = 0u;
static volatile uint16_t gps_tail = 0u;

static volatile uint8_t  co_ring[CO_RING_SIZE];
static volatile uint16_t co_head = 0u;
static volatile uint16_t co_tail = 0u;

/* ---------- DHT11 ---------- */
typedef struct
{
  uint8_t ok;
  uint8_t hum_int;
  uint8_t hum_dec;
  uint8_t temp_int;
  uint8_t temp_dec;
  uint8_t raw[5];
} DHT11_Data_t;

static DHT11_Data_t dht11;

/* ---------- ZE16B-CO ---------- */
static uint8_t  co_frame[9];
static uint8_t  co_index = 0u;
static uint8_t  co_valid = 0u;
static uint16_t co_ppm = 0u;
static uint32_t co_last_valid_ms = 0u;

/* ---------- GPS ---------- */
#define GPS_LINE_SIZE 128u
static char     gps_line[GPS_LINE_SIZE];
static uint16_t gps_line_len = 0u;

static uint8_t  gps_nmea_seen = 0u;
static uint8_t  gps_fix = 0u;
static uint8_t  gps_satellites = 0u;
static int32_t  gps_lat_e7 = 0;
static int32_t  gps_lon_e7 = 0;
static uint32_t gps_last_nmea_ms = 0u;

/* ---------- Application timing ---------- */
static uint32_t boot_ms = 0u;
static uint32_t last_status_ms = 0u;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);

/* USER CODE BEGIN PFP */
static void DWT_Delay_Init(void);
static void delay_us(uint32_t us);

static void DHT11_SetOutputOD(void);
static void DHT11_SetInput(void);
static uint8_t DHT11_Read(DHT11_Data_t *out);

static void GPS_Process(void);
static void GPS_ParseLine(char *line);
static int32_t NMEA_CoordToE7(const char *coord, char hemi, uint8_t *ok);
static uint8_t NMEA_ChecksumOK(const char *line);
static uint8_t GPS_GetField(const char *line, uint8_t index, char *out, uint16_t out_size);
static void FormatE7(int32_t value, char *out, uint16_t out_size);

static void CO_Process(void);
static uint8_t CO_Checksum(const uint8_t *frame);

static void Status_Print(void);
static void UART3_Print(const char *text);

static void GPS_RingPush(uint8_t b);
static uint8_t GPS_RingPop(uint8_t *b);
static void CO_RingPush(uint8_t b);
static uint8_t CO_RingPop(uint8_t *b);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

static void DWT_Delay_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
  uint32_t cycles_per_us = SystemCoreClock / 1000000u;
  uint32_t start = DWT->CYCCNT;
  uint32_t wait_cycles = us * cycles_per_us;

  while ((uint32_t)(DWT->CYCCNT - start) < wait_cycles)
  {
  }
}

static void DHT11_SetOutputOD(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = DHT11_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);
}

static void DHT11_SetInput(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = DHT11_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);
}

/* Wait until pin is NOT equal to 'state'. Returns 1 on success, 0 on timeout. */
static uint8_t DHT11_WaitWhile(GPIO_PinState state, uint32_t timeout_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t timeout_cycles = timeout_us * (SystemCoreClock / 1000000u);

  while (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) == state)
  {
    if ((uint32_t)(DWT->CYCCNT - start) > timeout_cycles)
    {
      return 0u;
    }
  }

  return 1u;
}

static uint8_t DHT11_Read(DHT11_Data_t *out)
{
  uint8_t data[5] = {0, 0, 0, 0, 0};
  uint8_t i;

  out->ok = 0u;

  /*
   * DHT11 start signal:
   * MCU pulls DATA low for >=18 ms, releases it, then reads 40 bits.
   */
  DHT11_SetOutputOD();
  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
  HAL_Delay(2);

  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);

  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
  delay_us(30);
  DHT11_SetInput();

  /* DHT11 response: ~80 us LOW, ~80 us HIGH */
  if (!DHT11_WaitWhile(GPIO_PIN_SET, 120u)) return 0u;
  if (!DHT11_WaitWhile(GPIO_PIN_RESET, 120u)) return 0u;
  if (!DHT11_WaitWhile(GPIO_PIN_SET, 120u)) return 0u;

  for (i = 0u; i < 40u; i++)
  {
    uint32_t high_start;
    uint32_t high_us;

    /* Each bit starts with ~50 us LOW. */
    if (!DHT11_WaitWhile(GPIO_PIN_RESET, 100u)) return 0u;

    /* Measure HIGH pulse. ~26-28 us = 0, ~70 us = 1. */
    high_start = DWT->CYCCNT;

    if (!DHT11_WaitWhile(GPIO_PIN_SET, 120u)) return 0u;

    high_us = (uint32_t)(DWT->CYCCNT - high_start) /
              (SystemCoreClock / 1000000u);

    data[i / 8u] <<= 1u;
    if (high_us > 50u)
    {
      data[i / 8u] |= 1u;
    }
  }

  DHT11_SetOutputOD();
  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);

  if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
  {
    memcpy(out->raw, data, 5u);
    return 0u;
  }

  memcpy(out->raw, data, 5u);
  out->hum_int  = data[0];
  out->hum_dec  = data[1];
  out->temp_int = data[2];
  out->temp_dec = data[3];
  out->ok = 1u;

  return 1u;
}

/* -------------------- Ring buffers -------------------- */

static void GPS_RingPush(uint8_t b)
{
  uint16_t next = (uint16_t)((gps_head + 1u) % GPS_RING_SIZE);

  if (next != gps_tail)
  {
    gps_ring[gps_head] = b;
    gps_head = next;
  }
}

static uint8_t GPS_RingPop(uint8_t *b)
{
  if (gps_tail == gps_head)
  {
    return 0u;
  }

  *b = gps_ring[gps_tail];
  gps_tail = (uint16_t)((gps_tail + 1u) % GPS_RING_SIZE);
  return 1u;
}

static void CO_RingPush(uint8_t b)
{
  uint16_t next = (uint16_t)((co_head + 1u) % CO_RING_SIZE);

  if (next != co_tail)
  {
    co_ring[co_head] = b;
    co_head = next;
  }
}

static uint8_t CO_RingPop(uint8_t *b)
{
  if (co_tail == co_head)
  {
    return 0u;
  }

  *b = co_ring[co_tail];
  co_tail = (uint16_t)((co_tail + 1u) % CO_RING_SIZE);
  return 1u;
}

/* -------------------- ZE16B-CO -------------------- */

static uint8_t CO_Checksum(const uint8_t *frame)
{
  uint8_t sum = 0u;
  uint8_t i;

  for (i = 1u; i <= 7u; i++)
  {
    sum = (uint8_t)(sum + frame[i]);
  }

  return (uint8_t)(~sum + 1u);
}

static void CO_Process(void)
{
  uint8_t b;

  while (CO_RingPop(&b))
  {
    if (co_index == 0u)
    {
      if (b == 0xFFu)
      {
        co_frame[0] = b;
        co_index = 1u;
      }
      continue;
    }

    co_frame[co_index++] = b;

    if (co_index >= 9u)
    {
      if ((co_frame[0] == 0xFFu) &&
          (co_frame[1] == 0x04u) &&
          (co_frame[2] == 0x03u) &&
          (CO_Checksum(co_frame) == co_frame[8]))
      {
        co_ppm = (uint16_t)(((uint16_t)co_frame[4] << 8u) | co_frame[5]);
        co_valid = 1u;
        co_last_valid_ms = HAL_GetTick();
      }

      /*
       * Re-sync. A valid ZE16B frame is exactly 9 bytes and begins with 0xFF.
       * If this frame was bad, simply search for the next 0xFF.
       */
      co_index = 0u;
    }
  }
}

/* -------------------- Air530 GPS / NMEA -------------------- */

static uint8_t hex_to_nibble(char c, uint8_t *ok)
{
  if ((c >= '0') && (c <= '9'))
  {
    *ok = 1u;
    return (uint8_t)(c - '0');
  }
  if ((c >= 'A') && (c <= 'F'))
  {
    *ok = 1u;
    return (uint8_t)(c - 'A' + 10);
  }
  if ((c >= 'a') && (c <= 'f'))
  {
    *ok = 1u;
    return (uint8_t)(c - 'a' + 10);
  }

  *ok = 0u;
  return 0u;
}

static uint8_t NMEA_ChecksumOK(const char *line)
{
  uint8_t checksum = 0u;
  const char *p;
  const char *star;
  uint8_t ok1, ok2;
  uint8_t expected;

  if ((line == NULL) || (line[0] != '$'))
  {
    return 0u;
  }

  star = strchr(line, '*');
  if (star == NULL)
  {
    /* Accept a sentence with no checksum field. */
    return 1u;
  }

  if ((star[1] == '\0') || (star[2] == '\0'))
  {
    return 0u;
  }

  for (p = line + 1; p < star; p++)
  {
    checksum ^= (uint8_t)(*p);
  }

  expected = (uint8_t)(hex_to_nibble(star[1], &ok1) << 4u);
  expected |= hex_to_nibble(star[2], &ok2);

  if ((!ok1) || (!ok2))
  {
    return 0u;
  }

  return (checksum == expected) ? 1u : 0u;
}

/*
 * Gets a comma-separated NMEA field while preserving empty fields.
 * Field 0 = sentence ID, field 1 = UTC, field 2 = latitude, ...
 */
static uint8_t GPS_GetField(const char *line, uint8_t index, char *out, uint16_t out_size)
{
  uint8_t current = 0u;
  uint16_t n = 0u;
  const char *p = line;

  if ((line == NULL) || (out == NULL) || (out_size == 0u))
  {
    return 0u;
  }

  while ((*p != '\0') && (current < index))
  {
    if (*p == ',')
    {
      current++;
    }
    p++;
  }

  if (current != index)
  {
    out[0] = '\0';
    return 0u;
  }

  while ((*p != '\0') && (*p != ',') && (*p != '*') &&
         (*p != '\r') && (*p != '\n'))
  {
    if (n < (uint16_t)(out_size - 1u))
    {
      out[n++] = *p;
    }
    p++;
  }

  out[n] = '\0';
  return 1u;
}

/*
 * Converts NMEA ddmm.mmmm / dddmm.mmmm into signed decimal degrees * 1e7.
 * Example output: 37.1234567 deg -> 371234567
 */
static int32_t NMEA_CoordToE7(const char *coord, char hemi, uint8_t *ok)
{
  const char *dot;
  int digits_before_dot;
  int degree_digits;
  int32_t degrees = 0;
  int32_t minutes_whole = 0;
  int32_t minutes_frac_e6 = 0;
  int frac_digits = 0;
  int i;
  int64_t minute_e6;
  int64_t result;

  *ok = 0u;

  if ((coord == NULL) || (coord[0] == '\0'))
  {
    return 0;
  }

  dot = strchr(coord, '.');
  if (dot == NULL)
  {
    return 0;
  }

  digits_before_dot = (int)(dot - coord);
  degree_digits = digits_before_dot - 2;

  if ((degree_digits != 2) && (degree_digits != 3))
  {
    return 0;
  }

  for (i = 0; i < degree_digits; i++)
  {
    if ((coord[i] < '0') || (coord[i] > '9')) return 0;
    degrees = (degrees * 10) + (coord[i] - '0');
  }

  for (i = degree_digits; i < digits_before_dot; i++)
  {
    if ((coord[i] < '0') || (coord[i] > '9')) return 0;
    minutes_whole = (minutes_whole * 10) + (coord[i] - '0');
  }

  dot++;
  while ((*dot >= '0') && (*dot <= '9') && (frac_digits < 6))
  {
    minutes_frac_e6 = (minutes_frac_e6 * 10) + (*dot - '0');
    frac_digits++;
    dot++;
  }

  while (frac_digits < 6)
  {
    minutes_frac_e6 *= 10;
    frac_digits++;
  }

  if (minutes_whole >= 60)
  {
    return 0;
  }

  minute_e6 = ((int64_t)minutes_whole * 1000000LL) + minutes_frac_e6;

  /* degrees*1e7 + (minutes/60)*1e7 = degrees*1e7 + minute_e6/6 */
  result = ((int64_t)degrees * 10000000LL) + (minute_e6 / 6LL);

  if ((hemi == 'S') || (hemi == 'W'))
  {
    result = -result;
  }
  else if ((hemi != 'N') && (hemi != 'E'))
  {
    return 0;
  }

  *ok = 1u;
  return (int32_t)result;
}

static void GPS_ParseLine(char *line)
{
  char fix_field[4];
  char lat_field[20];
  char ns_field[3];
  char lon_field[20];
  char ew_field[3];
  char sat_field[4];
  int fix_quality;
  int sats;
  uint8_t lat_ok, lon_ok;

  if (!NMEA_ChecksumOK(line))
  {
    return;
  }

  gps_nmea_seen = 1u;
  gps_last_nmea_ms = HAL_GetTick();

  /*
   * Match any talker ID that ends in GGA:
   * $GNGGA, $GPGGA, $GLGGA, $GAGGA, ...
   */
  if ((strlen(line) < 6u) ||
      (line[0] != '$') ||
      (line[3] != 'G') ||
      (line[4] != 'G') ||
      (line[5] != 'A'))
  {
    return;
  }

  GPS_GetField(line, 2u, lat_field, sizeof(lat_field));
  GPS_GetField(line, 3u, ns_field, sizeof(ns_field));
  GPS_GetField(line, 4u, lon_field, sizeof(lon_field));
  GPS_GetField(line, 5u, ew_field, sizeof(ew_field));
  GPS_GetField(line, 6u, fix_field, sizeof(fix_field));
  GPS_GetField(line, 7u, sat_field, sizeof(sat_field));

  fix_quality = atoi(fix_field);
  sats = atoi(sat_field);

  if (sats < 0) sats = 0;
  if (sats > 255) sats = 255;
  gps_satellites = (uint8_t)sats;

  if (fix_quality <= 0)
  {
    gps_fix = 0u;
    return;
  }

  gps_lat_e7 = NMEA_CoordToE7(lat_field, ns_field[0], &lat_ok);
  gps_lon_e7 = NMEA_CoordToE7(lon_field, ew_field[0], &lon_ok);

  if (lat_ok && lon_ok)
  {
    gps_fix = 1u;
  }
  else
  {
    gps_fix = 0u;
  }
}

static void GPS_Process(void)
{
  uint8_t b;

  while (GPS_RingPop(&b))
  {
    if (b == '$')
    {
      gps_line_len = 0u;
      gps_line[gps_line_len++] = (char)b;
      continue;
    }

    if (gps_line_len == 0u)
    {
      continue;
    }

    if (b == '\n')
    {
      gps_line[gps_line_len] = '\0';
      GPS_ParseLine(gps_line);
      gps_line_len = 0u;
      continue;
    }

    if (gps_line_len < (GPS_LINE_SIZE - 1u))
    {
      gps_line[gps_line_len++] = (char)b;
    }
    else
    {
      gps_line_len = 0u;
    }
  }
}

static void FormatE7(int32_t value, char *out, uint16_t out_size)
{
  int64_t v = value;
  uint32_t whole;
  uint32_t frac;

  if (v < 0)
  {
    v = -v;
    whole = (uint32_t)(v / 10000000LL);
    frac  = (uint32_t)(v % 10000000LL);
    snprintf(out, out_size, "-%lu.%07lu",
             (unsigned long)whole,
             (unsigned long)frac);
  }
  else
  {
    whole = (uint32_t)(v / 10000000LL);
    frac  = (uint32_t)(v % 10000000LL);
    snprintf(out, out_size, "%lu.%07lu",
             (unsigned long)whole,
             (unsigned long)frac);
  }
}

/* -------------------- TeraTerm status -------------------- */

static void UART3_Print(const char *text)
{
  HAL_UART_Transmit(&huart3,
                    (uint8_t *)text,
                    (uint16_t)strlen(text),
                    HAL_MAX_DELAY);
}

static void Status_Print(void)
{
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

  /* ZE16B-CO: official warm-up time = 30 s */
  if (elapsed_ms <= 30000u)
  {
    remaining_s = (30000u - elapsed_ms + 999u) / 1000u;
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "CO=WARMING_UP(%lus),",
                  (unsigned long)remaining_s);
  }
  else if (co_valid && ((uint32_t)(now - co_last_valid_ms) <= 3000u))
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "CO=%uppm,",
                  (unsigned)co_ppm);
  }
  else
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "CO=NOT_FOUND,");
  }

  /* GPS */
  if ((!gps_nmea_seen) || ((uint32_t)(now - gps_last_nmea_ms) > 5000u))
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "GPS=NOT_FOUND");
  }
  else if (!gps_fix)
  {
    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "GPS=NO_FIX,SAT=%u",
                  (unsigned)gps_satellites);
  }
  else
  {
    FormatE7(gps_lat_e7, lat, sizeof(lat));
    FormatE7(gps_lon_e7, lon, sizeof(lon));

    n += snprintf(&line[n], sizeof(line) - (size_t)n,
                  "GPS=FIX,LAT=%s,LON=%s,SAT=%u",
                  lat, lon,
                  (unsigned)gps_satellites);
  }

  snprintf(&line[n], sizeof(line) - (size_t)n, "\r\n");
  UART3_Print(line);
}

/* UART interrupt callbacks */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    GPS_RingPush(gps_rx_byte);
    HAL_UART_Receive_IT(&huart1, &gps_rx_byte, 1u);
  }
  else if (huart->Instance == USART2)
  {
    CO_RingPush(co_rx_byte);
    HAL_UART_Receive_IT(&huart2, &co_rx_byte, 1u);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    HAL_UART_Receive_IT(&huart1, &gps_rx_byte, 1u);
  }
  else if (huart->Instance == USART2)
  {
    HAL_UART_Receive_IT(&huart2, &co_rx_byte, 1u);
  }
}

/* USER CODE END 0 */

int main(void)
{
  MPU_Config();

  HAL_Init();

  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();

  /* USER CODE BEGIN 2 */
  DWT_Delay_Init();

  boot_ms = HAL_GetTick();
  last_status_ms = boot_ms - 2000u;

  /* Start continuous 1-byte interrupt reception. */
  if (HAL_UART_Receive_IT(&huart1, &gps_rx_byte, 1u) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UART_Receive_IT(&huart2, &co_rx_byte, 1u) != HAL_OK)
  {
    Error_Handler();
  }

  UART3_Print("\r\n=== SURVIVAL SENSOR START ===\r\n");
  UART3_Print("USART1=Air530 GPS 9600, USART2=ZE16B-CO 9600, USART3=TeraTerm 115200\r\n");
  /* USER CODE END 2 */

  while (1)
  {
    uint32_t now;

    GPS_Process();
    CO_Process();

    now = HAL_GetTick();

    /*
     * DHT11 should not be sampled too frequently.
     * Read + print once every 2 seconds.
     */
    if ((uint32_t)(now - last_status_ms) >= 2000u)
    {
      last_status_ms = now;

      (void)DHT11_Read(&dht11);

      /* Drain any UART bytes received by IRQ while DHT11 was being read. */
      GPS_Process();
      CO_Process();

      Status_Print();
    }
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* AXI clock gating */
  RCC->CKGAENR = 0xE003FFFF;

  /** Supply configuration update enable */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes CPU, AHB and APB bus clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 |
                                RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 |
                                RCC_CLOCKTYPE_D1PCLK1;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port,
                    DHT11_DATA_Pin,
                    GPIO_PIN_SET);

  GPIO_InitStruct.Pin = DHT11_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &GPIO_InitStruct);

  HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA0,
                               SYSCFG_SWITCH_PA0_CLOSE);
}

/* MPU Configuration */
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
