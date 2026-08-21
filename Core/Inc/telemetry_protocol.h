/**
  ******************************************************************************
  * @file    telemetry_protocol.h
  * @brief   Jetson 텔레메트리 프로토콜 v1 — JSONL + CRC-16/CCITT-FALSE 빌더와
  *          콘솔 명령 파서. HAL에 의존하지 않는 순수 C 모듈이라 호스트(gcc)에서
  *          같은 코드를 그대로 컴파일해 검증한다.
  *
  *          출력 계약의 정본은 프런트엔드 저장소의 MAP/gps_service.py
  *          (parse_stm32_telemetry / parse_stm32_output / parse_stm32_power_event)다.
  ******************************************************************************
  */

#ifndef TELEMETRY_PROTOCOL_H
#define TELEMETRY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TP_PROTOCOL_VERSION 1u

/* ---------- 콘솔 명령 ---------- */

typedef enum
{
  TP_CMD_EMPTY = 0,        /* 빈 줄 */
  TP_CMD_UNKNOWN,
  TP_CMD_PING,
  TP_CMD_STATUS,
  TP_CMD_GATE_ON,
  TP_CMD_GATE_OFF,
  TP_CMD_STREAM_ON,        /* Jetson GpsService가 접속 직후 전송 */
  TP_CMD_STREAM_OFF,
  TP_CMD_ALERT_TRAIL_ON,
  TP_CMD_ALERT_TRAIL_CAUTION,
  TP_CMD_ALERT_TRAIL_OFF,
  TP_CMD_POWER_OFF_ACK,
  TP_CMD_POWER_OFF_CANCEL
} TpCommand;

/* ---------- 텔레메트리 스냅샷 ---------- */

typedef enum
{
  TP_CO_LEVEL_UNKNOWN = 0,
  TP_CO_LEVEL_NORMAL,
  TP_CO_LEVEL_WARNING,
  TP_CO_LEVEL_ALARM
} TpCoLevel;

typedef enum
{
  TP_TRAIL_OFF = 0,
  TP_TRAIL_CAUTION,
  TP_TRAIL_ALERT
} TpTrailLevel;

typedef enum
{
  TP_POWER_STATE_GATE_ON = 0,
  TP_POWER_STATE_GATE_OFF,
  TP_POWER_STATE_STATUS
} TpPowerState;

typedef struct
{
  uint8_t  fix;            /* 0/1 */
  uint8_t  sats;
  uint8_t  has_age;        /* fix 좌표의 age_ms 유효 여부 */
  uint8_t  has_last_age;   /* no-fix 시 마지막 좌표 경과 시간 유효 여부 */
  int32_t  lat_e7;         /* fix=1일 때만 사용. 도 * 1e7 */
  int32_t  lon_e7;
  uint32_t age_ms;
  uint32_t last_age_ms;
} TpGps;

typedef struct
{
  uint8_t  valid;          /* DHT11 최신 판독 성공 여부 */
  uint8_t  temp_int;       /* DHT11 정수부/소수부 그대로 */
  uint8_t  temp_dec;
  uint8_t  hum_int;
  uint8_t  hum_dec;
  uint8_t  has_age;
  uint32_t age_ms;         /* 마지막 성공 판독 이후 경과 */
} TpEnv;

typedef struct
{
  uint8_t   valid;         /* 3초 이내 유효 ZE16B 프레임 존재 */
  uint8_t   warming_up;    /* 부팅 후 30초 예열 구간 */
  uint8_t   alarm;         /* 경보 부저 작동 중 (level=ALARM과 일치해야 함) */
  TpCoLevel level;
  uint16_t  ppm;           /* valid=1일 때만 사용 */
  uint8_t   has_age;
  uint32_t  age_ms;        /* 마지막 유효 프레임 이후 경과 */
} TpCo;

typedef struct
{
  uint8_t gate_on;           /* Jetson 전원 MOSFET gate 상태 */
  uint8_t shutdown_pending;  /* 전원 버튼 미구현 → 현재 항상 0 */
} TpPower;

typedef struct
{
  uint32_t seq;            /* 텔레메트리 전용 시퀀스. 줄마다 +1 (Jetson gap 판정) */
  uint32_t uptime_ms;
  TpGps    gps;
  TpEnv    env;
  TpCo     co;
  TpPower  power;
} TpTelemetry;

/* ---------- API ----------
 * 빌더 반환값: 생성된 줄 길이(개행 "\r\n" 포함, NUL 제외).
 * 버퍼가 부족하면 -1 — 호출자는 그 줄을 보내지 않아야 한다(부분 JSON 금지).
 */

uint16_t tp_crc16_ccitt(const uint8_t *data, size_t len);

int tp_build_telemetry(const TpTelemetry *t, char *out, size_t cap);

int tp_build_output_ack(uint32_t seq,
                        TpTrailLevel level,
                        uint32_t watchdog_ms,
                        char *out,
                        size_t cap);

int tp_build_power_event(uint32_t seq,
                         TpPowerState state,
                         uint8_t gate_on,
                         uint8_t shutdown_pending,
                         char *out,
                         size_t cap);

TpCommand tp_parse_command(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_PROTOCOL_H */
