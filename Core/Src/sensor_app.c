#include "sensor_app.h"

#include "air530_gps.h"
#include "dht11.h"
#include "ze16b_co.h"
#include "co_alarm.h"
#include "jetson_gate.h"
#include "console.h"
#include "telemetry_protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- 주기 · 임계 ---------- */
#define TELEMETRY_LINE_SIZE     512u
#define TELEMETRY_PERIOD_MS     2000u        /* DHT11 샘플링 + 텔레메트리 송출 주기 */
#define GPS_FRESH_MS            5000u        /* NMEA 신선 판정 */
#define TRAIL_WATCHDOG_MS       30000u       /* 무갱신 시 트레일 출력 자동 해제 */

/* ---------- 상태 ---------- */
static DHT11_Data_t dht11;
static uint8_t  dht_ever_ok = 0u;
static uint32_t dht_last_ok_ms = 0u;

static uint32_t boot_ms = 0u;
static uint32_t last_status_ms = 0u;

static uint8_t  stream_on = 1u;              /* 부팅 기본 ON — STREAM ON 유실에도 스트림 유지 */
static uint32_t telemetry_seq = 0u;          /* 텔레메트리 전용(줄마다 +1, Jetson gap 판정) */
static uint32_t event_seq = 0u;              /* output/power 이벤트 공용 */

static TpTrailLevel trail_level = TP_TRAIL_OFF;
static uint32_t trail_since_ms = 0u;

static char telemetry_line[TELEMETRY_LINE_SIZE];
static char cmd_line[CONSOLE_LINE_SIZE];

/* ---------- 사람용 상태 한 줄 (STATUS 명령) ---------- */

/* snprintf 누적의 size_t 언더플로를 막는 길이 가드 append. */
static int Line_Append(char *buf, int n, size_t cap, const char *fmt, ...)
{
  va_list args;
  int written;

  if ((n < 0) || ((size_t)n >= (cap - 1u)))
  {
    return (int)(cap - 1u);
  }

  va_start(args, fmt);
  written = vsnprintf(&buf[n], cap - (size_t)n, fmt, args);
  va_end(args);

  if (written < 0)
  {
    return n;
  }
  if ((size_t)((size_t)n + (size_t)written) >= cap)
  {
    return (int)(cap - 1u);
  }
  return n + written;
}

static void Status_Print(void)
{
  const Air530_Data_t *gps = Air530_GetData();
  const ZE16BCO_Data_t *co = ZE16BCO_GetData();
  CoAlarmState_t alarm = CoAlarm_GetState();

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
    n = Line_Append(line, n, sizeof(line),
                    "DHT11=OK,TEMP=%u.%uC,HUM=%u.%u%%,",
                    (unsigned)dht11.temp_int,
                    (unsigned)dht11.temp_dec,
                    (unsigned)dht11.hum_int,
                    (unsigned)dht11.hum_dec);
  }
  else
  {
    n = Line_Append(line, n, sizeof(line), "DHT11=ERROR,");
  }

  /* ZE16B-CO: official warm-up time = 30 s */
  if (CoAlarm_IsWarmingUp(now))
  {
    remaining_s = (CO_ALARM_WARMUP_MS - elapsed_ms + 999u) / 1000u;
    n = Line_Append(line, n, sizeof(line),
                    "CO=WARMING_UP(%lus),",
                    (unsigned long)remaining_s);
  }
  else if (CoAlarm_IsFresh(now, co))
  {
    n = Line_Append(line, n, sizeof(line),
                    "CO=%uppm,",
                    (unsigned)co->ppm);
  }
  else
  {
    n = Line_Append(line, n, sizeof(line), "CO=NOT_FOUND,");
  }

  /* GPS */
  if ((!gps->nmea_seen) || ((uint32_t)(now - gps->last_nmea_ms) > GPS_FRESH_MS))
  {
    n = Line_Append(line, n, sizeof(line), "GPS=NOT_FOUND");
  }
  else if (!gps->fix)
  {
    n = Line_Append(line, n, sizeof(line),
                    "GPS=NO_FIX,SAT=%u",
                    (unsigned)gps->satellites);
  }
  else
  {
    Air530_FormatE7(gps->lat_e7, lat, sizeof(lat));
    Air530_FormatE7(gps->lon_e7, lon, sizeof(lon));

    n = Line_Append(line, n, sizeof(line),
                    "GPS=FIX,LAT=%s,LON=%s,SAT=%u",
                    lat, lon,
                    (unsigned)gps->satellites);
  }

  /* CO 경보 상태와 Jetson 전원 게이트 */
  n = Line_Append(line, n, sizeof(line),
                  ",ALARM=%s,GATE=%s",
                  (alarm == CO_ALARM_ALARM) ? "ALARM"
                  : (alarm == CO_ALARM_WARN) ? "WARN" : "NONE",
                  JetsonGate_IsOn() ? "ON" : "OFF");

  (void)Line_Append(line, n, sizeof(line), "\r\n");
  Console_Print(line);
}

/* ---------- Jetson JSONL telemetry (protocol v1) ---------- */

/*
 * 현재 센서·게이트 상태를 프로토콜 스냅샷으로 옮긴다.
 * 없는 값은 만들지 않는다 — fix 없는 좌표, 실패한 DHT11 계측값,
 * 신선하지 않은 CO ppm은 필드 자체를 내보내지 않는다.
 */
static void Telemetry_Snapshot(TpTelemetry *t, uint32_t now)
{
  const Air530_Data_t *gps = Air530_GetData();
  const ZE16BCO_Data_t *co = ZE16BCO_GetData();
  CoAlarmState_t alarm = CoAlarm_GetState();
  uint8_t gps_fresh = (gps->nmea_seen &&
                       ((uint32_t)(now - gps->last_nmea_ms) <= GPS_FRESH_MS)) ? 1u : 0u;
  uint8_t co_fresh = CoAlarm_IsFresh(now, co);

  memset(t, 0, sizeof(*t));

  t->seq = telemetry_seq;
  t->uptime_ms = (uint32_t)(now - boot_ms);

  t->gps.fix = (gps_fresh && gps->fix) ? 1u : 0u;
  t->gps.sats = gps->satellites;
  if (t->gps.fix)
  {
    t->gps.lat_e7 = gps->lat_e7;
    t->gps.lon_e7 = gps->lon_e7;
    t->gps.has_age = 1u;
    t->gps.age_ms = (uint32_t)(now - gps->last_fix_ms);
  }
  else if (gps->ever_fix)
  {
    t->gps.has_last_age = 1u;
    t->gps.last_age_ms = (uint32_t)(now - gps->last_fix_ms);
  }

  t->env.valid = dht11.ok ? 1u : 0u;
  t->env.temp_int = dht11.temp_int;
  t->env.temp_dec = dht11.temp_dec;
  t->env.hum_int = dht11.hum_int;
  t->env.hum_dec = dht11.hum_dec;
  if (dht_ever_ok)
  {
    t->env.has_age = 1u;
    t->env.age_ms = (uint32_t)(now - dht_last_ok_ms);
  }

  t->co.valid = co_fresh;
  t->co.warming_up = CoAlarm_IsWarmingUp(now);
  t->co.alarm = (alarm == CO_ALARM_ALARM) ? 1u : 0u;
  if (alarm == CO_ALARM_ALARM)
  {
    t->co.level = TP_CO_LEVEL_ALARM;
  }
  else if (alarm == CO_ALARM_WARN)
  {
    t->co.level = TP_CO_LEVEL_WARNING;
  }
  else if (co_fresh)
  {
    t->co.level = TP_CO_LEVEL_NORMAL;
  }
  else
  {
    t->co.level = TP_CO_LEVEL_UNKNOWN;
  }
  t->co.ppm = co->ppm;
  if (co->valid)
  {
    t->co.has_age = 1u;
    t->co.age_ms = (uint32_t)(now - co->last_valid_ms);
  }

  t->power.gate_on = JetsonGate_IsOn();
  t->power.shutdown_pending = 0u;  /* 전원 버튼 미구현 */
}

static void Telemetry_Send(uint32_t now)
{
  TpTelemetry t;

  Telemetry_Snapshot(&t, now);
  if (tp_build_telemetry(&t, telemetry_line, sizeof(telemetry_line)) > 0)
  {
    telemetry_seq++;
    Console_Print(telemetry_line);
  }
}

static void Event_SendOutputAck(void)
{
  if (tp_build_output_ack(event_seq, trail_level, TRAIL_WATCHDOG_MS,
                          telemetry_line, sizeof(telemetry_line)) > 0)
  {
    event_seq++;
    Console_Print(telemetry_line);
  }
}

static void Event_SendPowerEvent(TpPowerState state)
{
  if (tp_build_power_event(event_seq, state, JetsonGate_IsOn(), 0u,
                           telemetry_line, sizeof(telemetry_line)) > 0)
  {
    event_seq++;
    Console_Print(telemetry_line);
  }
}

/*
 * 트레일 출력 상태 전이 + ACK. 진동 모터가 아직 실장되지 않아 물리 출력은
 * 없다 — 프로토콜 상태와 ACK만 유지한다(README "아직 구현하지 않은 것").
 */
static void Trail_Set(TpTrailLevel level)
{
  trail_level = level;
  trail_since_ms = HAL_GetTick();
  Event_SendOutputAck();
}

/* 트레일 출력 watchdog — Jetson 갱신이 끊기면 자동 해제하고 통지한다. */
static void Trail_Watchdog(uint32_t now)
{
  if ((trail_level != TP_TRAIL_OFF) &&
      ((uint32_t)(now - trail_since_ms) >= TRAIL_WATCHDOG_MS))
  {
    trail_level = TP_TRAIL_OFF;
    Event_SendOutputAck();
  }
}

/* ---------- 명령 처리 (Jetson 링크 UART4 · 미러 USART3) ---------- */

static void Command_Handle(const char *line)
{
  switch (tp_parse_command(line))
  {
    case TP_CMD_PING:
      Console_Print("PONG\r\n");
      break;
    case TP_CMD_STATUS:
      Status_Print();
      break;
    case TP_CMD_GATE_ON:
      JetsonGate_Set(1u);
      Console_Print("ACK GATE=ON\r\n");
      Event_SendPowerEvent(TP_POWER_STATE_GATE_ON);
      break;
    case TP_CMD_GATE_OFF:
      JetsonGate_Set(0u);
      Console_Print("ACK GATE=OFF\r\n");
      Event_SendPowerEvent(TP_POWER_STATE_GATE_OFF);
      break;
    case TP_CMD_STREAM_ON:
      /* Jetson이 접속 직후 보낸다. 텍스트 ACK 대신 즉시 텔레메트리 1줄로 응답
       * — Jetson 파서는 JSON이 아닌 줄을 거부하므로 사람용 ACK를 섞지 않는다. */
      stream_on = 1u;
      Telemetry_Send(HAL_GetTick());
      break;
    case TP_CMD_STREAM_OFF:
      /* 사람 콘솔 전용(TeraTerm 시연) — Jetson은 보내지 않는 명령. */
      stream_on = 0u;
      Console_Print("ACK STREAM=OFF\r\n");
      break;
    case TP_CMD_ALERT_TRAIL_ON:
      Trail_Set(TP_TRAIL_ALERT);
      break;
    case TP_CMD_ALERT_TRAIL_CAUTION:
      Trail_Set(TP_TRAIL_CAUTION);
      break;
    case TP_CMD_ALERT_TRAIL_OFF:
      Trail_Set(TP_TRAIL_OFF);
      break;
    case TP_CMD_POWER_OFF_ACK:
    case TP_CMD_POWER_OFF_CANCEL:
      /* 전원 버튼이 없어 종료 대기 상태 자체가 발생하지 않는다.
       * 프로토콜상 안전한 응답: 현재 gate 상태를 status 이벤트로 보고한다. */
      Event_SendPowerEvent(TP_POWER_STATE_STATUS);
      break;
    case TP_CMD_EMPTY:
      break;
    default:
      Console_Print("ERR UNKNOWN_CMD\r\n");
      break;
  }
}

static void Commands_Process(void)
{
  while (Console_ReadLine(cmd_line, sizeof(cmd_line)))
  {
    Command_Handle(cmd_line);
  }
}

/* ---------- 공개 API ---------- */

HAL_StatusTypeDef SensorApp_Init(UART_HandleTypeDef *gps_uart,
                                 UART_HandleTypeDef *co_uart,
                                 UART_HandleTypeDef *link_uart,
                                 UART_HandleTypeDef *mirror_uart)
{
  HAL_StatusTypeDef status;

  if ((gps_uart == NULL) || (co_uart == NULL) || (link_uart == NULL))
  {
    return HAL_ERROR;
  }

  status = Console_Init(link_uart, mirror_uart);
  if (status != HAL_OK)
  {
    return status;
  }

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
  last_status_ms = boot_ms - TELEMETRY_PERIOD_MS;

  CoAlarm_Init(boot_ms);
  JetsonGate_Init();  /* 부팅 시 Jetson 전원 ON (GATE OFF 명령으로 차단 데모 가능) */

  Console_Print("\r\n=== SURVIVAL SENSOR START ===\r\n");
  Console_Print("USART1=Air530 GPS 9600, USART2=ZE16B-CO 9600, "
                "UART4=Jetson JSONL 115200, USART3=console mirror 115200\r\n");
  Console_Print("CMD: PING | STATUS | GATE ON/OFF | STREAM ON/OFF"
                " | ALERT TRAIL ON/CAUTION/OFF | POWER OFF ACK/CANCEL\r\n");

  return HAL_OK;
}

void SensorApp_Process(void)
{
  uint32_t now;

  Air530_Process();
  ZE16BCO_Process();
  Commands_Process();

  now = HAL_GetTick();

  CoAlarm_Update(now, ZE16BCO_GetData());
  Trail_Watchdog(now);

  /*
   * DHT11 should not be sampled too frequently.
   * Read + emit one JSONL telemetry line every 2 seconds.
   */
  if ((uint32_t)(now - last_status_ms) >= TELEMETRY_PERIOD_MS)
  {
    last_status_ms = now;

    if (DHT11_Read(&dht11))
    {
      dht_ever_ok = 1u;
      dht_last_ok_ms = HAL_GetTick();
    }

    /* Drain UART bytes received by IRQ while DHT11 was being read. */
    Air530_Process();
    ZE16BCO_Process();

    if (stream_on)
    {
      Telemetry_Send(HAL_GetTick());
    }
  }
}

void SensorApp_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  Air530_RxCpltCallback(huart);
  ZE16BCO_RxCpltCallback(huart);
  Console_RxCpltCallback(huart);
}

void SensorApp_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  Air530_ErrorCallback(huart);
  ZE16BCO_ErrorCallback(huart);
  Console_ErrorCallback(huart);
}
