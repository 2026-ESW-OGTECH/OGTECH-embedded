#include "console.h"

#include <string.h>

#define CONSOLE_RING_SIZE 256u

static UART_HandleTypeDef *console_link = NULL;    /* Jetson 프로토콜 링크 */
static UART_HandleTypeDef *console_mirror = NULL;  /* 사람 콘솔(선택) */
static uint8_t console_rx_byte;                    /* 링크 1바이트 수신 버퍼 */
static uint8_t console_mirror_rx_byte;             /* 미러 1바이트 수신 버퍼 */

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

HAL_StatusTypeDef Console_Init(UART_HandleTypeDef *link, UART_HandleTypeDef *mirror)
{
  HAL_StatusTypeDef status;

  if (link == NULL)
  {
    return HAL_ERROR;
  }

  console_link = link;
  console_mirror = (mirror == link) ? NULL : mirror;
  console_head = 0u;
  console_tail = 0u;
  console_line_len = 0u;
  console_discard = 0u;

  status = HAL_UART_Receive_IT(console_link, &console_rx_byte, 1u);
  if (status != HAL_OK)
  {
    return status;
  }
  if (console_mirror != NULL)
  {
    /* 미러 수신 실패는 링크 동작을 막지 않는다 — 미러를 송신 전용으로 강등한다. */
    if (HAL_UART_Receive_IT(console_mirror, &console_mirror_rx_byte, 1u) != HAL_OK)
    {
      console_mirror_rx_byte = 0u;
    }
  }
  return HAL_OK;
}

void Console_Print(const char *text)
{
  uint16_t len;

  if ((console_link == NULL) || (text == NULL))
  {
    return;
  }

  len = (uint16_t)strlen(text);
  (void)HAL_UART_Transmit(console_link, (uint8_t *)text, len, HAL_MAX_DELAY);
  if (console_mirror != NULL)
  {
    (void)HAL_UART_Transmit(console_mirror, (uint8_t *)text, len, HAL_MAX_DELAY);
  }
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
      console_discard = 1u;  /* 63자 초과: 이 라인 전체를 폐기 모드로 전환 */
      console_line_len = 0u;
    }
  }

  return 0u;
}

void Console_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((console_link != NULL) && (huart == console_link))
  {
    Console_RingPush(console_rx_byte);
    (void)HAL_UART_Receive_IT(console_link, &console_rx_byte, 1u);
  }
  else if ((console_mirror != NULL) && (huart == console_mirror))
  {
    Console_RingPush(console_mirror_rx_byte);
    (void)HAL_UART_Receive_IT(console_mirror, &console_mirror_rx_byte, 1u);
  }
}

void Console_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((console_link != NULL) && (huart == console_link))
  {
    (void)HAL_UART_Receive_IT(console_link, &console_rx_byte, 1u);
  }
  else if ((console_mirror != NULL) && (huart == console_mirror))
  {
    (void)HAL_UART_Receive_IT(console_mirror, &console_mirror_rx_byte, 1u);
  }
}
