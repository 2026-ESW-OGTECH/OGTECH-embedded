/**
 * 펌웨어 호스트 시뮬레이션 테스트.
 *
 * mock main.h(HAL 대체) 위에서 Core/Src 전체(드라이버·통합 계층·main.c)를
 * 한 번역 단위로 그대로 컴파일해, HAL 콜백 → 링버퍼 → 파서 → 응답 경로를
 * 실제 코드로 구동한다. (같은 TU이므로 모듈의 static 상태를 직접 조작·검증한다.)
 *
 * 실행 모드
 *   인자 없음 : 자체 검증(assert) 수행
 *   --emit    : Jetson 계약 테스트용 시나리오 매트릭스의 USART3 출력을
 *               stdout으로 그대로 내보낸다 (test_protocol_contract.py가 소비)
 */

#define main firmware_main
#include "../../Core/Src/air530_gps.c"
#include "../../Core/Src/dht11.c"
#include "../../Core/Src/ze16b_co.c"
#include "../../Core/Src/co_alarm.c"
#include "../../Core/Src/jetson_gate.c"
#include "../../Core/Src/console.c"
#include "../../Core/Src/sensor_app.c"
#include "../../Core/Src/main.c"
#undef main

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond, label)                                        \
  do {                                                            \
    if (!(cond)) {                                                \
      printf("FAIL %s (line %d)\n", label, __LINE__);             \
      failures++;                                                 \
    }                                                             \
  } while (0)

/* ---------- 입력 주입 — 실제 HAL 콜백 경로를 탄다 ---------- */

/* Jetson이 UART4(링크)로 보내는 바이트. ISR(콜백)과 main loop 처리가 교차하도록
 * 바이트마다 명령 처리를 돌린다. */
static void feed_command(const char *bytes)
{
  const char *p;

  for (p = bytes; *p != '\0'; p++)
  {
    console_rx_byte = (uint8_t)*p;
    HAL_UART_RxCpltCallback(&huart4);
    Commands_Process();
  }
}

/* 링크로 바이트를 밀어 넣되 main loop는 돌리지 않는다 — 블로킹 송신(≈29 ms) 중
 * ISR만 도는 상황(링 버퍼 256 B) 재현용. */
static void feed_command_isr_only(const char *bytes)
{
  const char *p;

  for (p = bytes; *p != '\0'; p++)
  {
    console_rx_byte = (uint8_t)*p;
    HAL_UART_RxCpltCallback(&huart4);
  }
}

/* TeraTerm이 USART3(미러)로 보내는 바이트. */
static void feed_mirror_command(const char *bytes)
{
  const char *p;

  for (p = bytes; *p != '\0'; p++)
  {
    console_mirror_rx_byte = (uint8_t)*p;
    HAL_UART_RxCpltCallback(&huart3);
    Commands_Process();
  }
}

/* Air530이 USART1로 보내는 NMEA 문장. body는 '$'와 '*' 사이 본문. */
static void feed_nmea(const char *body)
{
  uint8_t checksum = 0u;
  const char *p;
  char tail[8];

  for (p = body; *p != '\0'; p++)
  {
    checksum ^= (uint8_t)*p;
  }
  snprintf(tail, sizeof(tail), "*%02X\r\n", (unsigned)checksum);

  gps_rx_byte = (uint8_t)'$';
  HAL_UART_RxCpltCallback(&huart1);
  for (p = body; *p != '\0'; p++)
  {
    gps_rx_byte = (uint8_t)*p;
    HAL_UART_RxCpltCallback(&huart1);
  }
  for (p = tail; *p != '\0'; p++)
  {
    gps_rx_byte = (uint8_t)*p;
    HAL_UART_RxCpltCallback(&huart1);
  }
  Air530_Process();
}

/* ZE16B-CO 9바이트 능동 업로드 프레임 (FF 04 03 .. high low .. cs). */
static void feed_co_frame(uint16_t ppm)
{
  uint8_t frame[9] = { 0xFFu, 0x04u, 0x03u, 0x00u,
                       (uint8_t)(ppm >> 8u), (uint8_t)(ppm & 0xFFu),
                       0x00u, 0x00u, 0x00u };
  uint8_t sum = 0u;
  int i;

  for (i = 1; i <= 7; i++)
  {
    sum = (uint8_t)(sum + frame[i]);
  }
  frame[8] = (uint8_t)(~sum + 1u);

  for (i = 0; i < 9; i++)
  {
    co_rx_byte = frame[i];
    HAL_UART_RxCpltCallback(&huart2);
  }
  ZE16BCO_Process();
}

/* ---------- 시나리오 상태 프리셋 (모듈 static 직접 설정) ---------- */

static void preset_boot_no_sensors(void)
{
  mock_tick_ms = 2000u;
  boot_ms = 0u;
  alarm_boot_ms = 0u;
  last_status_ms = mock_tick_ms;   /* 주기 송출이 테스트 중 끼어들지 않게 */
  memset(&gps_data, 0, sizeof(gps_data));
  memset(&co_data, 0, sizeof(co_data));
  memset(&dht11, 0, sizeof(dht11));
  dht_ever_ok = 0u;
  co_alarm_state = CO_ALARM_NONE;
  co_warn_tracking = 0u;
  co_clear_tracking = 0u;
  trail_level = TP_TRAIL_OFF;
  JetsonGate_Set(1u);
}

static void preset_full_sensors(void)
{
  mock_tick_ms = 100000u;  /* 예열(30초) 이후 */
  boot_ms = 0u;
  alarm_boot_ms = 0u;
  last_status_ms = mock_tick_ms;
  gps_data.nmea_seen = 1u;
  gps_data.last_nmea_ms = mock_tick_ms - 500u;
  gps_data.fix = 1u;
  gps_data.ever_fix = 1u;
  gps_data.last_fix_ms = mock_tick_ms - 500u;
  gps_data.satellites = 9u;
  gps_data.lat_e7 = 375417940;
  gps_data.lon_e7 = 1270795160;
  dht11.ok = 1u;
  dht11.temp_int = 24u;
  dht11.temp_dec = 3u;
  dht11.hum_int = 41u;
  dht11.hum_dec = 0u;
  dht_ever_ok = 1u;
  dht_last_ok_ms = mock_tick_ms - 100u;
  co_data.valid = 1u;
  co_data.ppm = 3u;
  co_data.last_valid_ms = mock_tick_ms - 200u;
  co_alarm_state = CO_ALARM_NONE;
  co_warn_tracking = 0u;
  co_clear_tracking = 0u;
  trail_level = TP_TRAIL_OFF;
  JetsonGate_Set(1u);
}

/* ---------- 자체 검증 ---------- */

static void test_init_banner(void)
{
  /* SensorApp_Init는 main()에서 호출 — 여기서는 mock Instance 설정 후 직접 호출 */
  mock_uart3_reset();
  CHECK(SensorApp_Init(&huart1, &huart2, &huart4, &huart3) == HAL_OK, "sensor app init");
  CHECK(strstr(mock_uart4_capture, "=== SURVIVAL SENSOR START ===") != NULL,
        "boot banner on jetson link");
  CHECK(strstr(mock_uart3_capture, "=== SURVIVAL SENSOR START ===") != NULL,
        "boot banner mirrored to console");
  CHECK(strcmp(mock_uart3_capture, mock_uart4_capture) == 0, "mirror equals link");
  CHECK(strstr(mock_uart3_capture, "STREAM ON/OFF") != NULL, "banner lists commands");
  CHECK(mock_gate_pin == GPIO_PIN_SET, "gate on at boot");
  CHECK(JetsonGate_IsOn() == 1u, "gate state at boot");
}

static void test_human_commands(void)
{
  preset_boot_no_sensors();

  mock_uart3_reset();
  feed_command("PING\r\n");
  CHECK(strcmp(mock_uart3_capture, "PONG\r\n") == 0, "ping -> pong");

  mock_uart3_reset();
  feed_command("BOGUS\n");
  CHECK(strcmp(mock_uart3_capture, "ERR UNKNOWN_CMD\r\n") == 0,
        "unknown command");

  mock_uart3_reset();
  feed_command("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA GATE OFF\n");
  CHECK(strcmp(mock_uart3_capture, "ERR LINE_TOO_LONG\r\n") == 0,
        "overlong line discarded");
  CHECK(JetsonGate_IsOn() == 1u, "overlong tail not executed");

  /* 미러(TeraTerm, USART3)로 들어온 명령도 같은 파서를 탄다 */
  mock_uart3_reset();
  feed_mirror_command("PING\r\n");
  CHECK(strcmp(mock_uart3_capture, "PONG\r\n") == 0, "mirror ping -> pong");
  CHECK(strcmp(mock_uart4_capture, "PONG\r\n") == 0, "mirror answer also on link");

  mock_uart3_reset();
  feed_command("STATUS\n");
  CHECK(strstr(mock_uart3_capture, "DHT11=ERROR") != NULL,
        "status stays human readable");
  CHECK(strstr(mock_uart3_capture, "CO=WARMING_UP(") != NULL,
        "status shows warm-up countdown");
  CHECK(mock_uart3_capture[0] != '{', "status is not JSONL");
}

static void test_stream_commands(void)
{
  preset_full_sensors();

  mock_uart3_reset();
  feed_command("STREAM OFF\n");
  CHECK(stream_on == 0u, "stream off flag");
  CHECK(strcmp(mock_uart3_capture, "ACK STREAM=OFF\r\n") == 0,
        "stream off human ack");

  mock_uart3_reset();
  feed_command("STREAM ON\n");
  CHECK(stream_on == 1u, "stream on flag");
  CHECK(mock_uart3_capture[0] == '{', "stream on answers with telemetry");
  CHECK(strstr(mock_uart3_capture, "\"event\":\"telemetry\"") != NULL,
        "stream on telemetry event");
  CHECK(strstr(mock_uart3_capture, "\"lat\":37.5417940") != NULL,
        "telemetry has fix coordinate");
  CHECK(strstr(mock_uart3_capture, "\"crc16\":\"") != NULL,
        "telemetry has crc16 tail");
}

static void test_periodic_loop_emits_telemetry(void)
{
  /* SensorApp_Process 경로: 2초 경과 시 DHT11 판독(호스트에선 실패) 후 JSONL 1줄 */
  uint32_t tick_at_process;

  preset_full_sensors();
  last_status_ms = mock_tick_ms - TELEMETRY_PERIOD_MS;
  tick_at_process = mock_tick_ms;
  mock_uart3_reset();
  SensorApp_Process();   /* 내부 DHT11_Read의 HAL_Delay가 mock tick을 22 ms 전진시킨다 */
  CHECK(strstr(mock_uart3_capture, "\"event\":\"telemetry\"") != NULL,
        "periodic telemetry emitted");
  CHECK(strstr(mock_uart3_capture, "\"env\":{\"valid\":false") != NULL,
        "host dht read fails honestly");
  CHECK(last_status_ms == tick_at_process, "period timer rearmed");

  mock_uart3_reset();
  SensorApp_Process();
  CHECK(mock_uart3_capture[0] == '\0', "no telemetry before next period");
}

static void test_nmea_path_to_telemetry(void)
{
  /* 실제 NMEA 바이트 → USART1 콜백 → 링버퍼 → GGA 파서 → 텔레메트리 좌표 */
  preset_boot_no_sensors();
  mock_tick_ms = 100000u;
  feed_nmea("GNGGA,123519,3732.5076,N,12704.7710,E,1,09,0.9,45.0,M,0.0,M,,");
  CHECK(gps_data.fix == 1u, "nmea fix parsed");
  CHECK(gps_data.satellites == 9u, "nmea sats parsed");
  CHECK(gps_data.ever_fix == 1u, "ever_fix set");

  mock_uart3_reset();
  Telemetry_Send(HAL_GetTick());
  CHECK(strstr(mock_uart3_capture, "\"fix\":true,\"lat\":37.54") != NULL,
        "telemetry lat from nmea");
  CHECK(strstr(mock_uart3_capture, "\"lon\":127.07") != NULL,
        "telemetry lon from nmea");

  /* 체크섬 틀린 문장은 거부 */
  gps_data.satellites = 9u;
  {
    const char *bad = "$GNGGA,123519,3732.5076,N,12704.7710,E,1,05,0.9,45.0,M,0.0,M,,*00\r\n";
    const char *p;
    for (p = bad; *p != '\0'; p++) { gps_rx_byte = (uint8_t)*p; HAL_UART_RxCpltCallback(&huart1); }
    Air530_Process();
  }
  CHECK(gps_data.satellites == 9u, "bad checksum sentence rejected");

  /* 체크섬 필드 자체가 없는 문장도 거부 (2026-08-30, WORKLOG #3) */
  {
    const char *nochk = "$GNGGA,123519,3732.5076,N,12704.7710,E,1,04,0.9,45.0,M,0.0,M,,\r\n";
    const char *p;
    for (p = nochk; *p != '\0'; p++) { gps_rx_byte = (uint8_t)*p; HAL_UART_RxCpltCallback(&huart1); }
    Air530_Process();
  }
  CHECK(gps_data.satellites == 9u, "sentence without checksum rejected");
}

static void test_link_ring_survives_blocking_tx(void)
{
  /* 텔레메트리 블로킹 송신(≈29 ms ≈ 334 B) 동안 Jetson 명령이 몰려도 링(256 B)이 받아 둔다.
   * 옛 32 B 링에서는 두 번째 명령부터 깨졌다(WORKLOG #10). */
  preset_full_sensors();
  mock_uart3_reset();
  feed_command_isr_only("ALERT TRAIL CAUTION\nPING\nPING\nALERT TRAIL ON\nPING\nGATE OFF\nPING\nSTATUS\nPING\n");
  Commands_Process();
  {
    int pongs = 0;
    const char *p = mock_uart4_capture;
    while ((p = strstr(p, "PONG\r\n")) != NULL) { pongs++; p += 6; }
    CHECK(pongs == 5, "all five PINGs answered after burst");
  }
  CHECK(trail_level == TP_TRAIL_ALERT, "burst commands applied in order");
  CHECK(JetsonGate_IsOn() == 0u, "gate off inside burst applied");
  CHECK(strstr(mock_uart4_capture, "ERR") == NULL, "no line error in burst");
  JetsonGate_Set(1u);
  trail_level = TP_TRAIL_OFF;
}

static void test_dht11_unlocks_dwt(void)
{
  mock_dwt.LAR = 0u;
  DHT11_Init();
  CHECK(mock_dwt.LAR == 0xC5ACCE55u, "dht11 init writes DWT LAR unlock key");
  CHECK((mock_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u, "cyccnt enabled");
}

static void test_co_frame_path_to_alarm(void)
{
  /* 실제 ZE16B 프레임 → USART2 콜백 → 파서 → 100 ppm 즉시 ALARM → 텔레메트리 */
  preset_full_sensors();
  feed_co_frame(150u);
  CHECK(co_data.valid == 1u && co_data.ppm == 150u, "co frame parsed");
  CoAlarm_Update(HAL_GetTick(), ZE16BCO_GetData());
  CHECK(CoAlarm_GetState() == CO_ALARM_ALARM, "100 ppm immediate alarm");

  mock_uart3_reset();
  Telemetry_Send(HAL_GetTick());
  CHECK(strstr(mock_uart3_capture,
               "\"level\":\"alarm\",\"alarm\":true,\"ppm\":150") != NULL,
        "co alarm level in telemetry");

  /* 경보 latched 상태에서 센서 단절: valid=false, ppm 미출력, 경보 유지 */
  co_data.last_valid_ms = mock_tick_ms - 10000u;
  CoAlarm_Update(HAL_GetTick(), ZE16BCO_GetData());
  CHECK(CoAlarm_GetState() == CO_ALARM_ALARM, "alarm latched when sensor stale");
  mock_uart3_reset();
  Telemetry_Send(HAL_GetTick());
  CHECK(strstr(mock_uart3_capture, "\"co\":{\"valid\":false") != NULL,
        "co stale invalid");
  CHECK(strstr(mock_uart3_capture, "\"ppm\":") == NULL, "co stale omits ppm");
  CHECK(strstr(mock_uart3_capture, "\"level\":\"alarm\",\"alarm\":true") != NULL,
        "co stale keeps alarm");

  /* 버저는 ALARM에서 200 ms 단속 — tick 100..199 구간은 ON */
  mock_tick_ms = 100300u;
  co_data.last_valid_ms = mock_tick_ms;
  co_data.ppm = 150u;
  CoAlarm_Update(HAL_GetTick(), ZE16BCO_GetData());
  CHECK(mock_buzzer_pin == GPIO_PIN_SET, "buzzer on in alarm pulse");
  mock_tick_ms = 100450u;
  CoAlarm_Update(HAL_GetTick(), ZE16BCO_GetData());
  CHECK(mock_buzzer_pin == GPIO_PIN_RESET, "buzzer off between pulses");
}

static void test_telemetry_sequence(void)
{
  uint32_t before = telemetry_seq;

  preset_full_sensors();
  mock_uart3_reset();
  Telemetry_Send(HAL_GetTick());
  Telemetry_Send(HAL_GetTick());
  CHECK(telemetry_seq == before + 2u, "telemetry seq increments per line");
}

static void test_trail_output(void)
{
  preset_full_sensors();

  mock_uart3_reset();
  feed_command("ALERT TRAIL CAUTION\n");
  CHECK(trail_level == TP_TRAIL_CAUTION, "trail caution state");
  CHECK(strstr(mock_uart3_capture, "\"event\":\"output\"") != NULL,
        "trail ack event");
  CHECK(strstr(mock_uart3_capture,
               "\"level\":\"caution\",\"active\":true") != NULL,
        "trail ack level");

  /* watchdog: 갱신 없이 30초 경과 → 자동 off + 통지 (SensorApp_Process 경로) */
  mock_uart3_reset();
  mock_tick_ms += TRAIL_WATCHDOG_MS + 1u;
  last_status_ms = mock_tick_ms;
  SensorApp_Process();
  CHECK(trail_level == TP_TRAIL_OFF, "trail watchdog clears");
  CHECK(strstr(mock_uart3_capture,
               "\"level\":\"off\",\"active\":false") != NULL,
        "trail watchdog notifies");
}

static void test_gate_and_power_events(void)
{
  preset_full_sensors();

  mock_uart3_reset();
  feed_command("GATE OFF\n");
  CHECK(JetsonGate_IsOn() == 0u, "gate off state");
  CHECK(mock_gate_pin == GPIO_PIN_RESET, "gate off pin");
  CHECK(strstr(mock_uart3_capture, "ACK GATE=OFF\r\n") != NULL,
        "gate off human ack");
  CHECK(strstr(mock_uart3_capture, "\"state\":\"gate_off\"") != NULL,
        "gate off power event");

  /* gate off 상태의 텔레메트리에도 반영 */
  mock_uart3_reset();
  Telemetry_Send(HAL_GetTick());
  CHECK(strstr(mock_uart3_capture, "\"jetson_gate_on\":false") != NULL,
        "telemetry reflects gate off");

  mock_uart3_reset();
  feed_command("GATE ON\n");
  CHECK(JetsonGate_IsOn() == 1u, "gate on state");
  CHECK(mock_gate_pin == GPIO_PIN_SET, "gate on pin");
  CHECK(strstr(mock_uart3_capture, "\"state\":\"gate_on\"") != NULL,
        "gate on power event");

  mock_uart3_reset();
  feed_command("POWER OFF ACK\n");
  CHECK(strstr(mock_uart3_capture, "\"state\":\"status\"") != NULL,
        "power ack answers status event");
  CHECK(strstr(mock_uart3_capture, "\"shutdown_pending\":false") != NULL,
        "no shutdown pending without button");
}

/* ---------- --emit: 계약 테스트용 시나리오 매트릭스 ---------- */

static void emit_capture(void)
{
  fputs(mock_uart4_capture, stdout);   /* Jetson 링크(UART4)에 실제로 나간 바이트 */
  mock_uart3_reset();
}

static void emit_matrix(void)
{
  /* 1. 부팅 직후(예열 중, 센서 전무) 주기 텔레메트리 */
  preset_boot_no_sensors();
  mock_uart3_reset();
  Telemetry_Send(HAL_GetTick());
  emit_capture();

  /* 2. 전 센서 정상 + fix */
  preset_full_sensors();
  Telemetry_Send(HAL_GetTick());
  emit_capture();

  /* 3. CO 경보 (100 ppm 즉시) — 실제 프레임 경로 */
  preset_full_sensors();
  feed_co_frame(150u);
  CoAlarm_Update(HAL_GetTick(), ZE16BCO_GetData());
  Telemetry_Send(HAL_GetTick());
  emit_capture();

  /* 4. WARN latched + 센서 단절 (valid=false, 경보 유지) */
  preset_full_sensors();
  co_alarm_state = CO_ALARM_WARN;
  co_data.last_valid_ms = mock_tick_ms - 10000u;
  Telemetry_Send(HAL_GetTick());
  emit_capture();

  /* 5. fix 상실 후 last_age 보고 */
  preset_full_sensors();
  gps_data.fix = 0u;
  gps_data.last_fix_ms = mock_tick_ms - 12000u;
  gps_data.satellites = 3u;
  Telemetry_Send(HAL_GetTick());
  emit_capture();

  /* 6. Jetson GpsService 접속 시퀀스: STREAM ON → 즉시 텔레메트리 */
  preset_full_sensors();
  feed_command("STREAM ON\n");
  emit_capture();

  /* 7. 트레일 출력 명령 3종 ACK */
  feed_command("ALERT TRAIL ON\n");
  feed_command("ALERT TRAIL CAUTION\n");
  feed_command("ALERT TRAIL OFF\n");
  emit_capture();

  /* 8. gate 전환 이벤트(+사람용 ACK 줄 혼재 — 파서 거부 경로 검증용) */
  feed_command("GATE OFF\n");
  Telemetry_Send(HAL_GetTick());
  feed_command("GATE ON\n");
  emit_capture();

  /* 9. 전원 handshake 응답 (버튼 미구현 → status 보고) */
  feed_command("POWER OFF ACK\n");
  feed_command("POWER OFF CANCEL\n");
  emit_capture();
}

int main(int argc, char **argv)
{
  /* firmware_main의 MX_USARTx_UART_Init 대신 최소 초기화 — 드라이버가
   * huart 포인터로 콜백을 식별하고, mock 캡처가 USART3 출력을 가려낸다. */
  huart1.Instance = USART1;
  huart2.Instance = USART2;
  huart3.Instance = USART3;
  huart4.Instance = UART4;

  if ((argc > 1) && (strcmp(argv[1], "--emit") == 0))
  {
    if (SensorApp_Init(&huart1, &huart2, &huart4, &huart3) != HAL_OK)
    {
      return EXIT_FAILURE;
    }
    emit_matrix();
    return EXIT_SUCCESS;
  }

  test_init_banner();
  test_human_commands();
  test_stream_commands();
  test_periodic_loop_emits_telemetry();
  test_nmea_path_to_telemetry();
  test_co_frame_path_to_alarm();
  test_telemetry_sequence();
  test_trail_output();
  test_gate_and_power_events();
  test_link_ring_survives_blocking_tx();
  test_dht11_unlocks_dwt();

  if (failures != 0)
  {
    printf("test_firmware_sim: %d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  printf("test_firmware_sim: all passed\n");
  return EXIT_SUCCESS;
}
