#include "air530_gps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_RING_SIZE 256u
#define GPS_LINE_SIZE 128u

static UART_HandleTypeDef *gps_uart = NULL;
static uint8_t gps_rx_byte;

static volatile uint8_t  gps_ring[GPS_RING_SIZE];
static volatile uint16_t gps_head = 0u;
static volatile uint16_t gps_tail = 0u;

static char     gps_line[GPS_LINE_SIZE];
static uint16_t gps_line_len = 0u;

static Air530_Data_t gps_data;

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
    /* Preserve original behavior: accept a sentence with no checksum field. */
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
static uint8_t GPS_GetField(const char *line,
                            uint8_t index,
                            char *out,
                            uint16_t out_size)
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

  gps_data.nmea_seen = 1u;
  gps_data.last_nmea_ms = HAL_GetTick();

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
  gps_data.satellites = (uint8_t)sats;

  if (fix_quality <= 0)
  {
    gps_data.fix = 0u;
    return;
  }

  gps_data.lat_e7 = NMEA_CoordToE7(lat_field, ns_field[0], &lat_ok);
  gps_data.lon_e7 = NMEA_CoordToE7(lon_field, ew_field[0], &lon_ok);

  if (lat_ok && lon_ok)
  {
    gps_data.fix = 1u;
  }
  else
  {
    gps_data.fix = 0u;
  }
}

HAL_StatusTypeDef Air530_Init(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return HAL_ERROR;
  }

  gps_uart = huart;
  gps_head = 0u;
  gps_tail = 0u;
  gps_line_len = 0u;
  memset(&gps_data, 0, sizeof(gps_data));

  return HAL_UART_Receive_IT(gps_uart, &gps_rx_byte, 1u);
}

void Air530_Process(void)
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

void Air530_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((gps_uart != NULL) && (huart == gps_uart))
  {
    GPS_RingPush(gps_rx_byte);
    (void)HAL_UART_Receive_IT(gps_uart, &gps_rx_byte, 1u);
  }
}

void Air530_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((gps_uart != NULL) && (huart == gps_uart))
  {
    (void)HAL_UART_Receive_IT(gps_uart, &gps_rx_byte, 1u);
  }
}

const Air530_Data_t *Air530_GetData(void)
{
  return &gps_data;
}

void Air530_FormatE7(int32_t value, char *out, uint16_t out_size)
{
  int64_t v = value;
  uint32_t whole;
  uint32_t frac;

  if ((out == NULL) || (out_size == 0u))
  {
    return;
  }

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
