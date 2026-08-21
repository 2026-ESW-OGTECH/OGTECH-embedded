/**
 * telemetry_protocol 단위 테스트 (호스트 gcc).
 * - CRC-16/CCITT-FALSE 표준 체크 벡터
 * - JSONL 골든 문자열 (필드 순서·서식·crc16 마감)
 * - 명령 파서 전 항목
 * - 버퍼 부족 시 -1 (부분 JSON 금지)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "telemetry_protocol.h"

static int failures = 0;

#define CHECK(cond, label)                                        \
  do {                                                            \
    if (!(cond)) {                                                \
      printf("FAIL %s (line %d)\n", label, __LINE__);             \
      failures++;                                                 \
    }                                                             \
  } while (0)

static void check_line_equals(const char *actual_label,
                              int built_len,
                              const char *line,
                              const char *expected_base)
{
  /* expected_base: crc16 필드 제외, 마지막 '}' 포함한 base JSON */
  char expected[600];
  uint16_t crc = tp_crc16_ccitt((const uint8_t *)expected_base,
                                strlen(expected_base));

  snprintf(expected, sizeof(expected), "%.*s,\"crc16\":\"%04X\"}\r\n",
           (int)(strlen(expected_base) - 1u), expected_base, crc);

  CHECK(built_len == (int)strlen(expected), actual_label);
  if (strcmp(line, expected) != 0)
  {
    printf("FAIL %s\n  expected: %s  actual:   %s", actual_label, expected, line);
    failures++;
  }
}

static void test_crc_vectors(void)
{
  /* CRC-16/CCITT-FALSE 공인 체크 값: "123456789" -> 0x29B1 */
  CHECK(tp_crc16_ccitt((const uint8_t *)"123456789", 9u) == 0x29B1u,
        "crc check vector");
  CHECK(tp_crc16_ccitt((const uint8_t *)"", 0u) == 0xFFFFu, "crc init value");
}

static void test_telemetry_full_fix(void)
{
  TpTelemetry t;
  char line[512];
  int n;

  memset(&t, 0, sizeof(t));
  t.seq = 17u;
  t.uptime_ms = 321000u;
  t.gps.fix = 1u;
  t.gps.lat_e7 = 375417940;
  t.gps.lon_e7 = 1270795160;
  t.gps.sats = 9u;
  t.gps.has_age = 1u;
  t.gps.age_ms = 1250u;          /* -> 1.3 (반올림) */
  t.env.valid = 1u;
  t.env.temp_int = 24u;
  t.env.temp_dec = 3u;
  t.env.hum_int = 41u;
  t.env.hum_dec = 0u;
  t.env.has_age = 1u;
  t.env.age_ms = 0u;
  t.co.valid = 1u;
  t.co.warming_up = 0u;
  t.co.alarm = 0u;
  t.co.level = TP_CO_LEVEL_NORMAL;
  t.co.ppm = 3u;
  t.co.has_age = 1u;
  t.co.age_ms = 400u;
  t.power.gate_on = 1u;
  t.power.shutdown_pending = 0u;

  n = tp_build_telemetry(&t, line, sizeof(line));
  CHECK(n > 0, "telemetry full build");
  check_line_equals(
      "telemetry full golden", n, line,
      "{\"v\":1,\"event\":\"telemetry\",\"seq\":17,\"uptime_ms\":321000,"
      "\"gps\":{\"fix\":true,\"lat\":37.5417940,\"lon\":127.0795160,"
      "\"sats\":9,\"age_s\":1.3},"
      "\"env\":{\"valid\":true,\"temp_c\":24.3,\"humidity_pct\":41.0,"
      "\"age_s\":0.0},"
      "\"co\":{\"valid\":true,\"warming_up\":false,\"level\":\"normal\","
      "\"alarm\":false,\"ppm\":3,\"age_s\":0.4},"
      "\"power\":{\"valid\":false,\"jetson_gate_on\":true,"
      "\"shutdown_pending\":false}}");
}

static void test_telemetry_all_invalid(void)
{
  TpTelemetry t;
  char line[512];
  int n;

  memset(&t, 0, sizeof(t));
  t.seq = 0u;
  t.uptime_ms = 2000u;
  t.co.warming_up = 1u;
  t.co.level = TP_CO_LEVEL_UNKNOWN;
  t.power.gate_on = 1u;

  n = tp_build_telemetry(&t, line, sizeof(line));
  CHECK(n > 0, "telemetry invalid build");
  check_line_equals(
      "telemetry invalid golden", n, line,
      "{\"v\":1,\"event\":\"telemetry\",\"seq\":0,\"uptime_ms\":2000,"
      "\"gps\":{\"fix\":false,\"sats\":0},"
      "\"env\":{\"valid\":false},"
      "\"co\":{\"valid\":false,\"warming_up\":true,\"level\":\"unknown\","
      "\"alarm\":false},"
      "\"power\":{\"valid\":false,\"jetson_gate_on\":true,"
      "\"shutdown_pending\":false}}");
}

static void test_telemetry_negative_coordinate_and_last_age(void)
{
  TpTelemetry t;
  char line[512];
  int n;

  memset(&t, 0, sizeof(t));
  t.seq = 5u;
  t.uptime_ms = 60000u;
  t.gps.fix = 0u;
  t.gps.sats = 3u;
  t.gps.has_last_age = 1u;
  t.gps.last_age_ms = 12000u;
  t.co.level = TP_CO_LEVEL_UNKNOWN;
  t.power.gate_on = 0u;

  n = tp_build_telemetry(&t, line, sizeof(line));
  CHECK(n > 0, "telemetry last_age build");
  CHECK(strstr(line, "\"gps\":{\"fix\":false,\"sats\":3,\"last_age_s\":12.0}")
            != NULL,
        "telemetry last_age field");
  CHECK(strstr(line, "\"jetson_gate_on\":false") != NULL,
        "telemetry gate off field");

  /* 남반구/서경 좌표 서식 */
  memset(&t, 0, sizeof(t));
  t.gps.fix = 1u;
  t.gps.lat_e7 = -371234567;
  t.gps.lon_e7 = -1270000001;
  t.co.level = TP_CO_LEVEL_UNKNOWN;
  n = tp_build_telemetry(&t, line, sizeof(line));
  CHECK(n > 0, "telemetry negative build");
  CHECK(strstr(line, "\"lat\":-37.1234567,\"lon\":-127.0000001") != NULL,
        "telemetry negative coordinates");
}

static void test_output_ack(void)
{
  char line[256];
  int n;

  n = tp_build_output_ack(3u, TP_TRAIL_CAUTION, 30000u, line, sizeof(line));
  CHECK(n > 0, "output ack build");
  check_line_equals(
      "output ack golden", n, line,
      "{\"v\":1,\"event\":\"output\",\"seq\":3,\"output\":\"trail\","
      "\"level\":\"caution\",\"active\":true,\"watchdog_ms\":30000}");

  n = tp_build_output_ack(4u, TP_TRAIL_OFF, 30000u, line, sizeof(line));
  CHECK(n > 0, "output ack off build");
  CHECK(strstr(line, "\"level\":\"off\",\"active\":false") != NULL,
        "output ack off inactive");

  CHECK(tp_build_output_ack(5u, (TpTrailLevel)9, 30000u, line, sizeof(line))
            == -1,
        "output ack invalid level");
}

static void test_power_event(void)
{
  char line[256];
  int n;

  n = tp_build_power_event(8u, TP_POWER_STATE_GATE_OFF, 0u, 0u,
                           line, sizeof(line));
  CHECK(n > 0, "power event build");
  check_line_equals(
      "power event golden", n, line,
      "{\"v\":1,\"event\":\"power\",\"seq\":8,\"state\":\"gate_off\","
      "\"gate_on\":false,\"shutdown_pending\":false}");

  CHECK(tp_build_power_event(9u, (TpPowerState)7, 1u, 0u, line, sizeof(line))
            == -1,
        "power event invalid state");
}

static void test_small_buffer_returns_error(void)
{
  TpTelemetry t;
  char line[64];

  memset(&t, 0, sizeof(t));
  CHECK(tp_build_telemetry(&t, line, sizeof(line)) == -1,
        "telemetry small buffer");
  CHECK(tp_build_output_ack(1u, TP_TRAIL_ALERT, 30000u, line, 10u) == -1,
        "output ack small buffer");
  CHECK(tp_build_power_event(1u, TP_POWER_STATE_STATUS, 1u, 0u, line, 10u)
            == -1,
        "power event small buffer");
}

static void test_command_parser(void)
{
  CHECK(tp_parse_command("PING") == TP_CMD_PING, "cmd ping");
  CHECK(tp_parse_command("STATUS") == TP_CMD_STATUS, "cmd status");
  CHECK(tp_parse_command("GATE ON") == TP_CMD_GATE_ON, "cmd gate on");
  CHECK(tp_parse_command("GATE OFF") == TP_CMD_GATE_OFF, "cmd gate off");
  CHECK(tp_parse_command("STREAM ON") == TP_CMD_STREAM_ON, "cmd stream on");
  CHECK(tp_parse_command("STREAM OFF") == TP_CMD_STREAM_OFF, "cmd stream off");
  CHECK(tp_parse_command("ALERT TRAIL ON") == TP_CMD_ALERT_TRAIL_ON,
        "cmd trail on");
  CHECK(tp_parse_command("ALERT TRAIL CAUTION") == TP_CMD_ALERT_TRAIL_CAUTION,
        "cmd trail caution");
  CHECK(tp_parse_command("ALERT TRAIL OFF") == TP_CMD_ALERT_TRAIL_OFF,
        "cmd trail off");
  CHECK(tp_parse_command("POWER OFF ACK") == TP_CMD_POWER_OFF_ACK,
        "cmd power ack");
  CHECK(tp_parse_command("POWER OFF CANCEL") == TP_CMD_POWER_OFF_CANCEL,
        "cmd power cancel");
  CHECK(tp_parse_command("") == TP_CMD_EMPTY, "cmd empty");
  CHECK(tp_parse_command(NULL) == TP_CMD_EMPTY, "cmd null");
  CHECK(tp_parse_command("ping") == TP_CMD_UNKNOWN, "cmd case sensitive");
  CHECK(tp_parse_command("GATE") == TP_CMD_UNKNOWN, "cmd prefix only");
  CHECK(tp_parse_command("GATE ON ") == TP_CMD_UNKNOWN, "cmd trailing space");
}

int main(void)
{
  test_crc_vectors();
  test_telemetry_full_fix();
  test_telemetry_all_invalid();
  test_telemetry_negative_coordinate_and_last_age();
  test_output_ack();
  test_power_event();
  test_small_buffer_returns_error();
  test_command_parser();

  if (failures != 0)
  {
    printf("test_protocol: %d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  printf("test_protocol: all passed\n");
  return EXIT_SUCCESS;
}
