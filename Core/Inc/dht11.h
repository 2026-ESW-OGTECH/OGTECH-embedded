#ifndef DHT11_H
#define DHT11_H

#include "main.h"
#include <stdint.h>

typedef struct
{
  uint8_t ok;
  uint8_t hum_int;
  uint8_t hum_dec;
  uint8_t temp_int;
  uint8_t temp_dec;
  uint8_t raw[5];
} DHT11_Data_t;

/* Initializes the Cortex DWT cycle counter used for microsecond timing. */
void DHT11_Init(void);

/* Reads one DHT11 sample. Returns 1 on success, 0 on timeout/checksum error. */
uint8_t DHT11_Read(DHT11_Data_t *out);

#endif /* DHT11_H */
