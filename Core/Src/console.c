#include "console.h"

#include <string.h>

#define CONSOLE_RING_SIZE 32u

static UART_HandleTypeDef *console_uart = NULL;
static uint8_t console_rx_byte;

static volatile uint8_t  console_ring[CONSOLE_RING_SIZE];
static volatile uint16_t console_head = 0u;
static volatile uint16_t console_tail = 0u;

static char     console_line[CONSOLE_LINE_SIZE];
static uint16_t console_line_len = 0u;
static uint8_t  console_discard = 0u;   /* 과길이 라인은 개행까지 통째로 버린다 */

static void Console_RingPush(uint8_t b)
{
  uint16_t next = (uint16_t)((console_head + 1u) % CONSOLE_RING_SIZE);

  if (next != console_tail)
  {
    console_ring[console_head] = b;
    console_head = next;
  }
}

static uint8_t Console_RingPop(uint8_t *b)
{
  if (console_tail == console_head)
  {
    return 0u;
  }

  *b = console_ring[console_tail];
  console_tail = (uint16_t)((console_tail + 1u) % CONSOLE_RING_SIZE);
  return 1u;
}

HAL_StatusTypeDef Console_Init(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return HAL_ERROR;
  }

  console_uart = huart;
  console_head = 0u;
  console_tail = 0u;
  console_line_len = 0u;
  console_discard = 0u;

  return HAL_UART_Receive_IT(console_uart, &console_rx_byte, 1u);
}

void Console_Print(const char *text)
{
  if ((console_uart == NULL) || (text == NULL))
  {
    return;
  }

  (void)HAL_UART_Transmit(console_uart,
                          (uint8_t *)text,
                          (uint16_t)strlen(text),
                          HAL_MAX_DELAY);
}

uint8_t Console_ReadLine(char *out, size_t cap)
{
  uint8_t b;

  if ((out == NULL) || (cap == 0u))
  {
    return 0u;
  }

  while (Console_RingPop(&b))
  {
    if ((b == (uint8_t)'\r') || (b == (uint8_t)'\n'))
    {
      if (console_discard)
      {
        /* 과길이 라인의 끝 - 꼬리를 명령으로 해석하지 않고 폐기한다. */
        console_discard = 0u;
        console_line_len = 0u;
        Console_Print("ERR LINE_TOO_LONG\r\n");
        continue;
      }
      if (console_line_len > 0u)
      {
        size_t n = console_line_len;

        if (n > (cap - 1u))
        {
          n = cap - 1u;
        }
        memcpy(out, console_line, n);
        out[n] = '\0';
        console_line_len = 0u;
        return 1u;
      }
      continue;
    }

    if (console_discard)
    {
      continue; /* 개행이 올 때까지 버린다. */
    }

    if (console_line_len < (CONSOLE_LINE_SIZE - 1u))
    {
      console_line[console_line_len++] = (char)b;
    }
    else
    {
      console_discard = 1u;  /* 31자 초과: 이 라인 전체를 폐기 모드로 전환 */
      console_line_len = 0u;
    }
  }

  return 0u;
}

void Console_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((console_uart != NULL) && (huart == console_uart))
  {
    Console_RingPush(console_rx_byte);
    (void)HAL_UART_Receive_IT(console_uart, &console_rx_byte, 1u);
  }
}

void Console_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((console_uart != NULL) && (huart == console_uart))
  {
    (void)HAL_UART_Receive_IT(console_uart, &console_rx_byte, 1u);
  }
}
