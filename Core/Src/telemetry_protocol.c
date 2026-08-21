/**
  ******************************************************************************
  * @file    telemetry_protocol.c
  * @brief   Jetson 텔레메트리 프로토콜 v1 구현. HAL 비의존 — 호스트 테스트가
  *          이 파일을 그대로 컴파일한다. 동적 할당·부동소수점 서식 미사용
  *          (newlib-nano의 %f 미지원을 피하려고 정수 스케일 서식만 쓴다).
  ******************************************************************************
  */

#include "telemetry_protocol.h"

#include <string.h>

/* ---------- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) ----------
 * gps_service.py의 crc16_ccitt와 같은 파라미터. base JSON(마지막 '}' 포함,
 * crc16 필드 제외)의 ASCII 바이트에 대해 계산한다.
 */
uint16_t tp_crc16_ccitt(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFFu;
  size_t i;
  uint8_t bit;

  for (i = 0u; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8u);
    for (bit = 0u; bit < 8u; bit++)
    {
      if ((crc & 0x8000u) != 0u)
      {
        crc = (uint16_t)((uint16_t)(crc << 1u) ^ 0x1021u);
      }
      else
      {
        crc = (uint16_t)(crc << 1u);
      }
    }
  }

  return crc;
}

/* ---------- 오버플로 안전 문자열 작성기 ---------- */

typedef struct
{
  char   *buf;
  size_t  cap;
  size_t  len;
  uint8_t ok;
} TpWriter;

static void tp_writer_init(TpWriter *w, char *buf, size_t cap)
{
  w->buf = buf;
  w->cap = cap;
  w->len = 0u;
  w->ok = (uint8_t)((buf != NULL) && (cap > 0u));
}

static void tp_put_char(TpWriter *w, char c)
{
  if (!w->ok)
  {
    return;
  }
  if (w->len + 1u >= w->cap)
  {
    w->ok = 0u;
    return;
  }
  w->buf[w->len++] = c;
  w->buf[w->len] = '\0';
}

static void tp_put(TpWriter *w, const char *s)
{
  size_t n;

  if (!w->ok)
  {
    return;
  }
  n = strlen(s);
  if (w->len + n >= w->cap)
  {
    w->ok = 0u;
    return;
  }
  memcpy(&w->buf[w->len], s, n + 1u);
  w->len += n;
}

static void tp_put_u32(TpWriter *w, uint32_t v)
{
  char tmp[11];
  int i = 0;

  do
  {
    tmp[i++] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v != 0u);

  while (i > 0)
  {
    tp_put_char(w, tmp[--i]);
  }
}

static void tp_put_bool(TpWriter *w, uint8_t v)
{
  tp_put(w, v ? "true" : "false");
}

/* 소수부 자릿수 고정 정수 스케일 서식: value=241, frac_digits=1 → "24.1" */
static void tp_put_scaled(TpWriter *w, int32_t value, uint8_t frac_digits)
{
  uint32_t magnitude;
  uint32_t divisor = 1u;
  uint8_t i;

  if (value < 0)
  {
    tp_put_char(w, '-');
    magnitude = (uint32_t)(-(int64_t)value);
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  for (i = 0u; i < frac_digits; i++)
  {
    divisor *= 10u;
  }

  tp_put_u32(w, magnitude / divisor);
  if (frac_digits > 0u)
  {
    uint32_t frac = magnitude % divisor;
    char tmp[10];

    tp_put_char(w, '.');
    for (i = frac_digits; i > 0u; i--)
    {
      tmp[i - 1u] = (char)('0' + (frac % 10u));
      frac /= 10u;
    }
    for (i = 0u; i < frac_digits; i++)
    {
      tp_put_char(w, tmp[i]);
    }
  }
}

/* 밀리초 → 초, 소수 1자리 반올림: 1234 ms → "1.2" */
static void tp_put_ms_as_s(TpWriter *w, uint32_t ms)
{
  uint32_t tenths = (ms + 50u) / 100u;

  tp_put_u32(w, tenths / 10u);
  tp_put_char(w, '.');
  tp_put_u32(w, tenths % 10u);
}

/* DHT11 정수부.소수부: (24, 3) → "24.3" */
static void tp_put_dht(TpWriter *w, uint8_t whole, uint8_t frac)
{
  tp_put_u32(w, whole);
  tp_put_char(w, '.');
  tp_put_u32(w, frac);
}

/* base JSON을 닫은 뒤 CRC를 계산해 `,"crc16":"XXXX"}` + CRLF로 마감한다. */
static int tp_finalize(TpWriter *w)
{
  static const char hex[] = "0123456789ABCDEF";
  uint16_t crc;

  tp_put_char(w, '}');
  if (!w->ok)
  {
    return -1;
  }

  crc = tp_crc16_ccitt((const uint8_t *)w->buf, w->len);

  /* 마지막 '}'를 crc16 필드로 대체한다. */
  w->len -= 1u;
  w->buf[w->len] = '\0';
  tp_put(w, ",\"crc16\":\"");
  tp_put_char(w, hex[(crc >> 12u) & 0xFu]);
  tp_put_char(w, hex[(crc >> 8u) & 0xFu]);
  tp_put_char(w, hex[(crc >> 4u) & 0xFu]);
  tp_put_char(w, hex[crc & 0xFu]);
  tp_put(w, "\"}\r\n");

  return w->ok ? (int)w->len : -1;
}

/* ---------- 이벤트 빌더 ---------- */

int tp_build_telemetry(const TpTelemetry *t, char *out, size_t cap)
{
  TpWriter w;
  static const char *co_levels[] = { "unknown", "normal", "warning", "alarm" };

  if (t == NULL)
  {
    return -1;
  }

  tp_writer_init(&w, out, cap);

  tp_put(&w, "{\"v\":");
  tp_put_u32(&w, TP_PROTOCOL_VERSION);
  tp_put(&w, ",\"event\":\"telemetry\",\"seq\":");
  tp_put_u32(&w, t->seq);
  tp_put(&w, ",\"uptime_ms\":");
  tp_put_u32(&w, t->uptime_ms);

  /* gps — fix가 아니면 좌표를 내보내지 않는다(추측 좌표 금지). */
  tp_put(&w, ",\"gps\":{\"fix\":");
  tp_put_bool(&w, t->gps.fix);
  if (t->gps.fix)
  {
    tp_put(&w, ",\"lat\":");
    tp_put_scaled(&w, t->gps.lat_e7, 7u);
    tp_put(&w, ",\"lon\":");
    tp_put_scaled(&w, t->gps.lon_e7, 7u);
  }
  tp_put(&w, ",\"sats\":");
  tp_put_u32(&w, t->gps.sats);
  if (t->gps.fix && t->gps.has_age)
  {
    tp_put(&w, ",\"age_s\":");
    tp_put_ms_as_s(&w, t->gps.age_ms);
  }
  if (!t->gps.fix && t->gps.has_last_age)
  {
    tp_put(&w, ",\"last_age_s\":");
    tp_put_ms_as_s(&w, t->gps.last_age_ms);
  }
  tp_put_char(&w, '}');

  /* env — DHT11. 판독 실패 시 계측값을 내보내지 않는다. */
  tp_put(&w, ",\"env\":{\"valid\":");
  tp_put_bool(&w, t->env.valid);
  if (t->env.valid)
  {
    tp_put(&w, ",\"temp_c\":");
    tp_put_dht(&w, t->env.temp_int, t->env.temp_dec);
    tp_put(&w, ",\"humidity_pct\":");
    tp_put_dht(&w, t->env.hum_int, t->env.hum_dec);
  }
  if (t->env.has_age)
  {
    tp_put(&w, ",\"age_s\":");
    tp_put_ms_as_s(&w, t->env.age_ms);
  }
  tp_put_char(&w, '}');

  /* co — ZE16B. valid가 아니면 ppm을 내보내지 않는다. */
  tp_put(&w, ",\"co\":{\"valid\":");
  tp_put_bool(&w, t->co.valid);
  tp_put(&w, ",\"warming_up\":");
  tp_put_bool(&w, t->co.warming_up);
  tp_put(&w, ",\"level\":\"");
  tp_put(&w, co_levels[(t->co.level <= TP_CO_LEVEL_ALARM) ? t->co.level
                                                          : TP_CO_LEVEL_UNKNOWN]);
  tp_put(&w, "\",\"alarm\":");
  tp_put_bool(&w, t->co.alarm);
  if (t->co.valid)
  {
    tp_put(&w, ",\"ppm\":");
    tp_put_u32(&w, t->co.ppm);
  }
  if (t->co.has_age)
  {
    tp_put(&w, ",\"age_s\":");
    tp_put_ms_as_s(&w, t->co.age_ms);
  }
  tp_put_char(&w, '}');

  /* power — 배터리 계측 하드웨어가 없어 valid=false 고정. gate 상태는 실값. */
  tp_put(&w, ",\"power\":{\"valid\":false,\"jetson_gate_on\":");
  tp_put_bool(&w, t->power.gate_on);
  tp_put(&w, ",\"shutdown_pending\":");
  tp_put_bool(&w, t->power.shutdown_pending);
  tp_put_char(&w, '}');

  return tp_finalize(&w);
}

int tp_build_output_ack(uint32_t seq,
                        TpTrailLevel level,
                        uint32_t watchdog_ms,
                        char *out,
                        size_t cap)
{
  TpWriter w;
  static const char *levels[] = { "off", "caution", "alert" };

  if (level > TP_TRAIL_ALERT)
  {
    return -1;
  }

  tp_writer_init(&w, out, cap);

  tp_put(&w, "{\"v\":");
  tp_put_u32(&w, TP_PROTOCOL_VERSION);
  tp_put(&w, ",\"event\":\"output\",\"seq\":");
  tp_put_u32(&w, seq);
  tp_put(&w, ",\"output\":\"trail\",\"level\":\"");
  tp_put(&w, levels[level]);
  tp_put(&w, "\",\"active\":");
  tp_put_bool(&w, (uint8_t)(level != TP_TRAIL_OFF));
  tp_put(&w, ",\"watchdog_ms\":");
  tp_put_u32(&w, watchdog_ms);

  return tp_finalize(&w);
}

int tp_build_power_event(uint32_t seq,
                         TpPowerState state,
                         uint8_t gate_on,
                         uint8_t shutdown_pending,
                         char *out,
                         size_t cap)
{
  TpWriter w;
  static const char *states[] = { "gate_on", "gate_off", "status" };

  if (state > TP_POWER_STATE_STATUS)
  {
    return -1;
  }

  tp_writer_init(&w, out, cap);

  tp_put(&w, "{\"v\":");
  tp_put_u32(&w, TP_PROTOCOL_VERSION);
  tp_put(&w, ",\"event\":\"power\",\"seq\":");
  tp_put_u32(&w, seq);
  tp_put(&w, ",\"state\":\"");
  tp_put(&w, states[state]);
  tp_put(&w, "\",\"gate_on\":");
  tp_put_bool(&w, gate_on);
  tp_put(&w, ",\"shutdown_pending\":");
  tp_put_bool(&w, shutdown_pending);

  return tp_finalize(&w);
}

/* ---------- 콘솔 명령 파서 ---------- */

TpCommand tp_parse_command(const char *line)
{
  static const struct
  {
    const char *text;
    TpCommand   command;
  } table[] = {
    { "PING",                TP_CMD_PING },
    { "STATUS",              TP_CMD_STATUS },
    { "GATE ON",             TP_CMD_GATE_ON },
    { "GATE OFF",            TP_CMD_GATE_OFF },
    { "STREAM ON",           TP_CMD_STREAM_ON },
    { "STREAM OFF",          TP_CMD_STREAM_OFF },
    { "ALERT TRAIL ON",      TP_CMD_ALERT_TRAIL_ON },
    { "ALERT TRAIL CAUTION", TP_CMD_ALERT_TRAIL_CAUTION },
    { "ALERT TRAIL OFF",     TP_CMD_ALERT_TRAIL_OFF },
    { "POWER OFF ACK",       TP_CMD_POWER_OFF_ACK },
    { "POWER OFF CANCEL",    TP_CMD_POWER_OFF_CANCEL },
  };
  size_t i;

  if ((line == NULL) || (line[0] == '\0'))
  {
    return TP_CMD_EMPTY;
  }

  for (i = 0u; i < (sizeof(table) / sizeof(table[0])); i++)
  {
    if (strcmp(line, table[i].text) == 0)
    {
      return table[i].command;
    }
  }

  return TP_CMD_UNKNOWN;
}
