#include "ze16b_co.h"

#include <string.h>

#define CO_RING_SIZE 64u
#define CO_FRAME_SIZE 9u

static UART_HandleTypeDef *co_uart = NULL;
static uint8_t co_rx_byte;

static volatile uint8_t  co_ring[CO_RING_SIZE];
static volatile uint16_t co_head = 0u;
static volatile uint16_t co_tail = 0u;

static uint8_t co_frame[CO_FRAME_SIZE];
static uint8_t co_index = 0u;

static ZE16BCO_Data_t co_data;

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

HAL_StatusTypeDef ZE16BCO_Init(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return HAL_ERROR;
  }

  co_uart = huart;
  co_head = 0u;
  co_tail = 0u;
  co_index = 0u;
  memset(&co_data, 0, sizeof(co_data));

  return HAL_UART_Receive_IT(co_uart, &co_rx_byte, 1u);
}

void ZE16BCO_Process(void)
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

    if (co_index >= CO_FRAME_SIZE)
    {
      if ((co_frame[0] == 0xFFu) &&
          (co_frame[1] == 0x04u) &&
          (co_frame[2] == 0x03u) &&
          (CO_Checksum(co_frame) == co_frame[8]))
      {
        co_data.ppm = (uint16_t)(((uint16_t)co_frame[4] << 8u) |
                                 co_frame[5]);
        co_data.valid = 1u;
        co_data.last_valid_ms = HAL_GetTick();
      }

      /*
       * Re-sync: a frame is exactly 9 bytes and begins with 0xFF.
       * If the frame was bad, search for the next 0xFF.
       */
      co_index = 0u;
    }
  }
}

void ZE16BCO_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((co_uart != NULL) && (huart == co_uart))
  {
    CO_RingPush(co_rx_byte);
    (void)HAL_UART_Receive_IT(co_uart, &co_rx_byte, 1u);
  }
}

void ZE16BCO_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((co_uart != NULL) && (huart == co_uart))
  {
    (void)HAL_UART_Receive_IT(co_uart, &co_rx_byte, 1u);
  }
}

const ZE16BCO_Data_t *ZE16BCO_GetData(void)
{
  return &co_data;
}
