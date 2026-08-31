/**
 * 호스트 테스트용 mock main.h — Core/Src 모듈들이 include하는 CubeIDE 생성
 * main.h를 대신한다. HAL 심볼을 시뮬레이션 훅으로 치환해 펌웨어 전체를
 * gcc로 컴파일·구동한다. 실제 보드 동작을 대체하지 않는다(실장 검증 별도).
 */

#ifndef HOST_MOCK_MAIN_H
#define HOST_MOCK_MAIN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---------- 공통 타입 ---------- */

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

typedef struct
{
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
} GPIO_InitTypeDef;

typedef struct
{
  uint32_t BaudRate;
  uint32_t WordLength;
  uint32_t StopBits;
  uint32_t Parity;
  uint32_t Mode;
  uint32_t HwFlowCtl;
  uint32_t OverSampling;
  uint32_t OneBitSampling;
  uint32_t ClockPrescaler;
} UART_InitTypeDef;

typedef struct
{
  uint32_t AdvFeatureInit;
} UART_AdvFeatureInitTypeDef;

typedef struct
{
  void *Instance;
  UART_InitTypeDef Init;
  UART_AdvFeatureInitTypeDef AdvancedInit;
} UART_HandleTypeDef;

typedef struct
{
  uint32_t OscillatorType;
  uint32_t HSIState;
  uint32_t HSICalibrationValue;
  struct { uint32_t PLLState; } PLL;
} RCC_OscInitTypeDef;

typedef struct
{
  uint32_t ClockType;
  uint32_t SYSCLKSource;
  uint32_t SYSCLKDivider;
  uint32_t AHBCLKDivider;
  uint32_t APB3CLKDivider;
  uint32_t APB1CLKDivider;
  uint32_t APB2CLKDivider;
  uint32_t APB4CLKDivider;
} RCC_ClkInitTypeDef;

typedef struct
{
  uint8_t  Enable;
  uint8_t  Number;
  uint32_t BaseAddress;
  uint8_t  Size;
  uint8_t  SubRegionDisable;
  uint8_t  TypeExtField;
  uint8_t  AccessPermission;
  uint8_t  DisableExec;
  uint8_t  IsShareable;
  uint8_t  IsCacheable;
  uint8_t  IsBufferable;
} MPU_Region_InitTypeDef;

/* ---------- 페리페럴 식별자 ---------- */

#define USART1 ((void *)0x1001)
#define USART2 ((void *)0x1002)
#define USART3 ((void *)0x1003)
#define UART4  ((void *)0x1004)

#define GPIOA ((void *)0xA000)
#define GPIOB ((void *)0xB000)
#define GPIOC ((void *)0xC000)
#define GPIOD ((void *)0xD000)

#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_4  ((uint16_t)0x0010)
#define GPIO_PIN_9  ((uint16_t)0x0200)

#define DHT11_DATA_GPIO_Port GPIOA
#define DHT11_DATA_Pin       GPIO_PIN_4

/* ---------- 상수 (값 자체는 의미 없음) ---------- */

#define GPIO_MODE_INPUT        0u
#define GPIO_MODE_OUTPUT_PP    1u
#define GPIO_MODE_OUTPUT_OD    2u
#define GPIO_NOPULL            0u
#define GPIO_SPEED_FREQ_LOW    0u

#define UART_WORDLENGTH_8B          0u
#define UART_STOPBITS_1             0u
#define UART_PARITY_NONE            0u
#define UART_MODE_TX_RX             0u
#define UART_HWCONTROL_NONE         0u
#define UART_OVERSAMPLING_16        0u
#define UART_ONE_BIT_SAMPLE_DISABLE 0u
#define UART_PRESCALER_DIV1         0u
#define UART_ADVFEATURE_NO_INIT     0u
#define UART_TXFIFO_THRESHOLD_1_8   0u
#define UART_RXFIFO_THRESHOLD_1_8   0u
#define HAL_MAX_DELAY               0xFFFFFFFFu

#define RCC_OSCILLATORTYPE_HSI 0u
#define RCC_HSI_DIV1           0u
#define RCC_PLL_NONE           0u
#define RCC_CLOCKTYPE_HCLK     0x01u
#define RCC_CLOCKTYPE_SYSCLK   0x02u
#define RCC_CLOCKTYPE_PCLK1    0x04u
#define RCC_CLOCKTYPE_PCLK2    0x08u
#define RCC_CLOCKTYPE_D3PCLK1  0x10u
#define RCC_CLOCKTYPE_D1PCLK1  0x20u
#define RCC_SYSCLKSOURCE_HSI   0u
#define RCC_SYSCLK_DIV1        0u
#define RCC_HCLK_DIV1          0u
#define RCC_APB3_DIV1          0u
#define RCC_APB1_DIV1          0u
#define RCC_APB2_DIV1          0u
#define RCC_APB4_DIV1          0u
#define FLASH_LATENCY_2        0u

#define MPU_REGION_ENABLE             1u
#define MPU_REGION_NUMBER0            0u
#define MPU_REGION_SIZE_4GB           0u
#define MPU_TEX_LEVEL0                0u
#define MPU_REGION_NO_ACCESS          0u
#define MPU_INSTRUCTION_ACCESS_DISABLE 1u
#define MPU_ACCESS_SHAREABLE          1u
#define MPU_ACCESS_NOT_CACHEABLE      0u
#define MPU_ACCESS_NOT_BUFFERABLE     0u
#define MPU_PRIVILEGED_DEFAULT        0u

#define PWR_DIRECT_SMPS_SUPPLY        0u
#define PWR_REGULATOR_VOLTAGE_SCALE3  0u
#define PWR_FLAG_VOSRDY               0u
#define SYSCFG_SWITCH_PA0             0u
#define SYSCFG_SWITCH_PA0_CLOSE       0u

#define CoreDebug_DEMCR_TRCENA_Msk 0x01000000u
#define DWT_CTRL_CYCCNTENA_Msk     0x00000001u

/* ---------- mock 레지스터 블록 ---------- */

typedef struct { volatile uint32_t DEMCR; } MockCoreDebug;
typedef struct { volatile uint32_t CYCCNT; volatile uint32_t CTRL; volatile uint32_t LAR; } MockDwt;
typedef struct { volatile uint32_t CKGAENR; } MockRcc;

static MockCoreDebug mock_core_debug;
static MockDwt       mock_dwt;
static MockRcc       mock_rcc;

/* DWT 접근마다 사이클 카운터를 크게 전진시켜 delay_us/타임아웃 스핀 루프가
 * 호스트에서 즉시 끝나게 한다(DHT11 판독은 타임아웃·체크섬 실패 경로로 종료). */
static inline MockDwt *mock_dwt_access(void)
{
  mock_dwt.CYCCNT += 64000u;
  return &mock_dwt;
}

#define CoreDebug (&mock_core_debug)
#define DWT       (mock_dwt_access())
#define RCC       (&mock_rcc)

static uint32_t SystemCoreClock = 64000000u;

/* ---------- 시뮬레이션 상태 (테스트가 직접 읽고 조작) ---------- */

static uint32_t mock_tick_ms = 0u;

#define MOCK_UART3_CAP 8192u
static char   mock_uart3_capture[MOCK_UART3_CAP];   /* USART3 = 사람 콘솔 미러 */
static size_t mock_uart3_len = 0u;
static char   mock_uart4_capture[MOCK_UART3_CAP];   /* UART4 = Jetson 링크(JSONL) */
static size_t mock_uart4_len = 0u;

static GPIO_PinState mock_gate_pin = GPIO_PIN_RESET;
/* 구 부저 핀. 경보음이 Jetson 스피커로 옮겨간 뒤로는 아무도 쓰지 않아야 한다. */
static GPIO_PinState mock_pb0_pin = GPIO_PIN_RESET;
static uint32_t      mock_pb0_writes = 0u;
static GPIO_PinState mock_dht_read_state = GPIO_PIN_RESET;

static inline void mock_uart3_reset(void)
{
  mock_uart3_len = 0u;
  mock_uart3_capture[0] = '\0';
  mock_uart4_len = 0u;
  mock_uart4_capture[0] = '\0';
}

/* ---------- 함수형 매크로 ---------- */

#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(x) ((void)0)
#define __HAL_PWR_GET_FLAG(f) (1)
#define __disable_irq() ((void)0)
#define __enable_irq()  ((void)0)

/* ---------- mock HAL 함수 ---------- */

static inline void HAL_Init(void) {}
static inline void HAL_Delay(uint32_t ms) { mock_tick_ms += ms; }
static inline uint32_t HAL_GetTick(void) { return mock_tick_ms; }

static inline void HAL_PWREx_ConfigSupply(uint32_t s) { (void)s; }
static inline HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *c) { (void)c; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *c, uint32_t l) { (void)c; (void)l; return HAL_OK; }

static inline void HAL_MPU_Disable(void) {}
static inline void HAL_MPU_ConfigRegion(MPU_Region_InitTypeDef *c) { (void)c; }
static inline void HAL_MPU_Enable(uint32_t m) { (void)m; }
static inline void HAL_SYSCFG_AnalogSwitchConfig(uint32_t s, uint32_t st) { (void)s; (void)st; }

static inline void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *init)
{
  (void)port;
  (void)init;
}

static inline void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state)
{
  if ((port == GPIOC) && (pin == GPIO_PIN_9))
  {
    mock_gate_pin = state;
  }
  else if ((port == GPIOB) && (pin == GPIO_PIN_0))
  {
    mock_pb0_pin = state;
    mock_pb0_writes += 1u;
  }
}

/* DHT11 대기 루프가 무한 대기하지 않도록 호출마다 토글한다.
 * (호스트에서 DHT11 판독은 항상 체크섬 실패 → ok=0 경로로 끝난다.) */
static inline GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
  (void)port;
  (void)pin;
  mock_dwt.CYCCNT += 1000u;  /* 타임아웃 판정도 전진시킨다 */
  mock_dht_read_state = (mock_dht_read_state == GPIO_PIN_SET) ? GPIO_PIN_RESET
                                                              : GPIO_PIN_SET;
  return mock_dht_read_state;
}

static inline HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_UARTEx_SetTxFifoThreshold(UART_HandleTypeDef *h, uint32_t t) { (void)h; (void)t; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_UARTEx_SetRxFifoThreshold(UART_HandleTypeDef *h, uint32_t t) { (void)h; (void)t; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_UARTEx_DisableFifoMode(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *b, uint16_t n) { (void)h; (void)b; (void)n; return HAL_OK; }

static inline HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h,
                                                  uint8_t *data,
                                                  uint16_t len,
                                                  uint32_t timeout)
{
  (void)timeout;
  if ((h->Instance == USART3) &&
      ((mock_uart3_len + (size_t)len) < (MOCK_UART3_CAP - 1u)))
  {
    memcpy(&mock_uart3_capture[mock_uart3_len], data, len);
    mock_uart3_len += len;
    mock_uart3_capture[mock_uart3_len] = '\0';
  }
  else if ((h->Instance == UART4) &&
           ((mock_uart4_len + (size_t)len) < (MOCK_UART3_CAP - 1u)))
  {
    memcpy(&mock_uart4_capture[mock_uart4_len], data, len);
    mock_uart4_len += len;
    mock_uart4_capture[mock_uart4_len] = '\0';
  }
  return HAL_OK;
}

void Error_Handler(void);

#endif /* HOST_MOCK_MAIN_H */
