# STM32F401RE 상시 센서 허브

> 폴더 이름은 구 도메인 이력 때문에 유지한다. 현재 펌웨어에는 서보·트레이·재고 센서 기능이 없다.

## 핀 배치

| 인터페이스 | STM32 핀 | 장치 |
|---|---|---|
| USART1 RX/TX | PA10 / PA9 | Air530, 9600 8N1 |
| USART6 RX/TX | PC7 / PC6 | ZE07-CO, 9600 8N1 |
| USART2 RX/TX | PA3 / PA2 | Jetson, 115200 8N1 |
| I2C1 SDA/SCL | PB9 / PB8 | SHT40 `0x44`, DS3231 `0x68`, BMP390 `0x76` 또는 `0x77` |
| GPIO | PB0 / PB1 / PC8 | 부저 / 진동 / 스트로브 드라이버 |
| GPIO 출력 | PC9 | Jetson 전원 MOSFET gate, active-high |
| GPIO 입력 | PA0 / PA1 / PA4 | 전원 / 체크포인트 / 음성 버튼, active-low |

GPIO는 부하를 직접 구동하지 않는다. 트랜지스터/MOSFET, 저항, 모터 역기전력 다이오드를 사용한다.
세 버튼은 STM32 내부 pull-up을 사용하며 GND로 닫히는 IP67 순간 스위치를 연결한다. 40 ms debounce 뒤
`pressed/released`를 각각 한 번 보내며, release에는 누른 시간 `held_ms`가 포함된다 `[출처: 펌웨어]`.

## 텔레메트리

1초마다 GPS·환경·CO·전원 자리표시자를 한 줄 JSON으로 전송한다 `[출처: 펌웨어]`. 마지막 `crc16`은
그 필드를 붙이기 전 JSON 전체에 대한 CRC-16/CCITT-FALSE다.

```json
{"v":1,"event":"telemetry","seq":1,"uptime_ms":1000,"gps":{"fix":false,"sats":0,"hdop":null,"acc_m":null,"age_s":null,"last_age_s":null},"rtc":{"valid":true,"iso_utc":"2026-08-19T06:30:00Z","age_s":0.1},"env":{"valid":true,"sht_valid":true,"pressure_valid":true,"temp_c":23.45,"humidity_pct":58.20,"press_hpa":1007.40,"press_trend":"unknown","age_s":0.1,"press_age_s":0.1,"bmp_address":119},"co":{"valid":false,"warming_up":true,"ppm":3.2,"level":"normal","alarm":false,"age_s":0.1},"power":{"valid":false,"percent":null,"days_left":null,"jetson_gate_on":true,"shutdown_pending":false},"crc16":"0000"}
```

예시의 CRC `0000`은 형식 자리표시자이며 실제 출력값이 아니다.

버튼 이벤트도 같은 CRC 규칙을 사용한다. 좌표나 임의 명령 문자열은 포함하지 않는다.

```json
{"v":1,"event":"button","seq":3,"button":"voice","state":"released","held_ms":1840,"crc16":"0000"}
```

트레일 출력 ACK와 watchdog 자동 해제 이벤트도 `v/seq/crc16`을 포함한다. 따라서 Jetson은 단순 송신과
STM32 수신 확인을 구분한다 `[출처: 펌웨어]`.

DS3231은 oscillator-stop flag가 꺼져 있고 달력 범위가 유효할 때만 `valid=true`다. 자동으로 시스템
시각을 RTC에 쓰지 않으며, 출고·정비 단계에서 UTC로 설정해야 한다. BMP390은 Bosch 보정식이 포함된
Adafruit BMP3XX 2.1.6으로 읽는다. 연결이 없거나 3회 연속 읽기 실패 시 `pressure_valid=false`이며
기압이나 추세를 확정하지 않는다. 센서 재초기화 때는 이전 연결 세션의 값과 추세 표본을 전부 버리고
새 표본으로 최소 10분을 다시 채운다 `[출처: 펌웨어]`.

기압 추세는 1분 간격 최근 31개 관측을 사용하고, 최소 10분이 모이기 전에는 `unknown`이다. 이후
최소제곱 기울기가 시간당 +0.5 hPa 초과면 `rising`, -0.5 hPa 미만이면 `falling`, 그 사이면
`steady`로 표시한다. 이 임계값은 현장 평가 전 프로젝트 기준이다 `[추정]`.

전원 버튼을 2초 이상 누른 뒤 놓으면 STM32가 `shutdown_requested`를 보내지만 전원을 즉시 끊지는
않는다. Jetson의 로컬 전원 관리 서비스가 검증된 pending에 `POWER OFF ACK`를 먼저 보내야만 90초 뒤
PC9를 LOW로 내린다. 이어지는 systemd 종료 요청이 실패하면 `POWER OFF CANCEL`이 예약을 취소한다.
ACK가 120초 안에 오지 않으면 요청을 취소한다. 짧게 눌러 꺼진 gate를 켜는
경로와 이 종료 handshake 모두 실제 MOSFET·NVMe에서 검증 전이다 `[미검증]`.

## CO 경보

- 주의: 35 ppm 3분 또는 최근 10분 최저 대비 +20 ppm `[추정: 프로젝트 기준]`
- 경보: 100 ppm 즉시, 또는 70 ppm/60분·150 ppm/10분·400 ppm/4분 `[출처: 프로젝트 기준]`
- 해제: 30 ppm 미만 30초 `[추정: 래치 해제 기준]`
- 첫 5분: 화면 상태는 예열. 단, 100 ppm 이상은 물리 경보 `[추정: 안전 편향]`

이미 발생한 경보는 CO 직렬 입력이 끊겼다는 이유만으로 해제하지 않는다.

트레일 이탈 진동은 Jetson 지도 엔진이 이탈 상태를 계산한 뒤 `ALERT TRAIL ON` 또는
`ALERT TRAIL CAUTION`을 2초마다 갱신하는 보조 출력이다. 펌웨어는 마지막 갱신 뒤 5초가 지나면 이
진동만 자동 해제한다
`[출처: 펌웨어]`. CO 경보·주의 출력은 트레일 watchdog과 독립이며 항상 우선한다.

## 명령

```text
PING
STREAM ON
STREAM OFF
GET_TELEMETRY
GET_FIX
ALERT TRAIL ON
ALERT TRAIL CAUTION
ALERT TRAIL OFF
POWER OFF ACK
POWER OFF CANCEL
POWER STATUS
SET RTC UTC 2026-08-19T06:30:00Z
```

`GET_FIX`는 기존 Jetson 파서 호환용이다. fix가 없으면 마지막 좌표 경과 시간만 보내고 좌표를 추정하지 않는다.
`ALERT TRAIL ON/CAUTION/OFF`는 지도 엔진이 계산한 이탈 상태만 전달하며 좌표·거리·방위 인자를 받지
않는다. `ON`은 정확도를 포함해 임계 초과가 확인된 때의 2회 진동, `CAUTION`은 정확도 미확정 상태에서
임계의 2배를 넘은 보수적 추정의 1회 짧은 진동이다 `[출처: 펌웨어]`.
`SET RTC UTC`는 출고·정비용 로컬 직렬 명령이며 정확히 `YYYY-MM-DDTHH:MM:SSZ` 형식만 허용한다.
MAP·LLM API에는 이 명령을 노출하지 않는다 `[출처: 펌웨어·MAP 출력 allowlist]`.

자세한 결선과 Jetson 실행은
[`STM32_JETSON_SETUP.md`](../../OGTECH-llm/MAP/STM32_JETSON_SETUP.md)를 따른다.

## 빌드 확인

```bash
arduino-cli lib install "Adafruit BMP3XX Library@2.1.6"
arduino-cli compile \
  --fqbn STMicroelectronics:stm32:Nucleo_64:pnum=NUCLEO_F401RE \
  stm32_smart_tray_controller
```

`Adafruit BMP3XX Library@2.1.6`은 `Adafruit Unified Sensor@1.1.15`와
`Adafruit BusIO@1.17.4`를 함께 설치한다 `[출처: Arduino Library Index, 2026-08-19]`.

현재 설치된 STM32duino 도구체인에서 성공했고 플래시 55,600 B(10%), 전역 RAM 6,020 B(6%)를 사용했다
`[실측: 2026-08-19]`.
