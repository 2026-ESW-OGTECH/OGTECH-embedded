# OGTECH-embedded — STM32 상시 계층 펌웨어

**OGTECH Kit** (2026 임베디드 소프트웨어 경진대회 자유공모 / 팀 OGTECH) 의 펌웨어 저장소입니다.
[조직 개요](https://github.com/2026-ESW-OGTECH) · [다른 저장소 안내](https://github.com/2026-ESW-OGTECH/.github)

---

## 목표 한 줄

**Jetson 전원이 꺼져 있어도 일산화탄소를 감시하고 경보를 울린다.**

밀폐된 텐트 안의 연소기구는 수면 중에 초기 증상을 자각할 수 없다는 점이 가장 위험합니다.
그래서 감시 주체를 Jetson이 아니라 저전력 MCU로 내립니다.
MCU가 Jetson 전원을 MOSFET로 물리 차단한 채 상시 동작하고, CO 임계를 넘으면
Jetson 부팅을 기다리지 않고 직접 경보를 냅니다.

이 이중 전원 구조가 이 작품의 해자입니다. 지도 앱으로는 흉내 낼 수 없고, 장기 운용도 여기서만 성립합니다.

## 대상 보드 — NUCLEO-H7A3ZI-Q

MCU는 **STM32H7A3ZI-Q**입니다. `Core/Src/main.c`가 H7 전용 API를 씁니다.

```text
PWR_DIRECT_SMPS_SUPPLY           SMPS 전원 공급
RCC_CLOCKTYPE_D1PCLK1 / D3PCLK1  D1·D3 도메인 클럭
UART_PRESCALER_DIV1              UART 프리스케일러
HAL_UARTEx_Set*FifoThreshold     UART FIFO 임계
MPU_Config()                     MPU 영역 설정
HAL_SYSCFG_AnalogSwitchConfig    PA0 아날로그 스위치
```

클럭은 PLL을 쓰지 않고(`RCC_PLL_NONE`) HSI로 동작합니다.
`HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA0, ...)`는 NUCLEO-H7A3ZI-Q의 PA0_C 아날로그 스위치 설정입니다.

> 2026-08-20 이전 문서에는 MCU가 `STM32F401RET6`으로 적혀 있었습니다. **실제 보드와 달랐고,
> 이 저장소의 코드는 처음부터 H7용이었습니다.** 관련 문서를 전부 H7A3ZI-Q 기준으로 고쳤습니다.
> 전력 예산만은 F4 기준으로 계산돼 있어 재측정이 남았습니다(아래 검증 상태 참조).

## 현재 펌웨어가 하는 일

STM32 HAL(STM32CubeIDE) 기반입니다. 인터럽트 수신 + 링 버퍼로 두 UART 센서를 동시에 받고,
2초마다 Jetson 파서와 계약된 **JSONL + CRC16 텔레메트리 한 줄**을 출력합니다
(사람용 상태 줄은 `STATUS` 명령으로 즉시 확인).

| 계통 | 부품 | 인터페이스 | 구현 내용 |
|---|---|---|---|
| 안전 | **ZE16B-CO** | USART2 · 9600 | 9바이트 프레임 동기화(`FF 04 03`), 체크섬 검증, ppm 산출 |
| 안전 | **CO 경보 판정 + 부저** | GPIO PB0 | 35 ppm 3분 지속 WARN · 100 ppm 즉시 ALARM · 30 ppm 미만 30초 해제 |
| 전원 | **Jetson MOSFET gate** | GPIO PC9 | active-high 게이트 제어. 부팅 시 ON, `GATE ON/OFF` 명령으로 전환 |
| 환경 | **DHT11** | GPIO 단선 | DWT 마이크로초 지연 기반 비트뱅잉, 5바이트 판독 (판독 구간 인터럽트 차단) |
| 측위 | **Air530 GNSS** | USART1 · 9600 | NMEA 체크섬 검증(무체크섬 문장 거부), fix·위성 수·위경도(E7) 파싱 |
| 출력 | Jetson/콘솔 | USART3 · 115200 | 2초 주기 JSONL+CRC16 텔레메트리(프로토콜 v1), `STREAM`/`ALERT TRAIL`/`POWER OFF`/`GATE`/`PING`/`STATUS` 명령 수신 |

## 구성

```text
Core/
├─ Inc/                    모듈 인터페이스 (헤더 8개)
└─ Src/
   ├─ air530_gps.c         NMEA 파싱 · 링 버퍼 · 좌표 변환 · fix 시각
   ├─ dht11.c              비트뱅잉 판독 · DWT 마이크로초 지연
   ├─ ze16b_co.c           9바이트 프레임 · 체크섬 · ppm
   ├─ co_alarm.c           CO 경보 상태기(latched) · 부저 PB0
   ├─ jetson_gate.c        Jetson 전원 MOSFET gate PC9
   ├─ console.c            USART3 수신 링 버퍼 · 줄 조립 · 과길이 폐기 · 송신
   ├─ telemetry_protocol.c Jetson 프로토콜 v1 — JSONL+CRC16 빌더 · 명령 파서 (HAL 비의존)
   ├─ sensor_app.c         통합 계층 — 2초 주기 · 명령 처리 · 텔레메트리 스냅샷 · watchdog
   └─ main.c               CubeMX/HAL 초기화와 진입점 (USER CODE 영역만 사용)
tests/
├─ host/                   gcc 호스트 테스트 — mock HAL 위에서 Core/Src 전체를 컴파일·시뮬레이션
│  └─ run_host_tests.sh    C 단위(프로토콜) + 펌웨어 시뮬레이션 실행기
└─ test_protocol_contract.py  시뮬레이터 출력을 실제 Jetson 파서(gps_service.py)로 왕복 검증
```

**드라이버와 프로토콜은 MCU 의존 코드에서 분리돼 있습니다.** 보드를 옮길 때 `main.c`만 CubeMX로
다시 생성하고 나머지 모듈은 그대로 이식합니다. 모듈이 스스로 결정하는 핀은 PB0(부저)과 PC9(gate)
두 개뿐이며 각 모듈의 `*_Init()`이 설정합니다.

| 모듈 | 하는 일 |
|---|---|
| `dht11` | GPIO 모드 전환, DWT 사이클 카운터 기반 µs 지연, 40비트 수신, 체크섬 검증 |
| `air530_gps` | UART RX 인터럽트 바이트 버퍼링(256 B 링), NMEA 체크섬 검증, GGA 파싱, `도 × 1e7` 변환, fix·위성 수·마지막 fix 시각 관리 |
| `ze16b_co` | UART RX 인터럽트 바이트 버퍼링(64 B 링), 9바이트 프레임 동기화·체크섬, ppm 산출 |
| `co_alarm` | 35/100/30 ppm 경보 상태기(센서 단절 시 latched 유지), 부저 패턴 구동, 예열·신선도 판정 |
| `jetson_gate` | PC9 gate 초기화(부팅 ON)·전환·상태 조회 |
| `console` | USART3 RX 링 버퍼(32 B)·개행 종결 줄 조립·31자 초과 줄 폐기(`ERR LINE_TOO_LONG`)·블로킹 송신 |
| `telemetry_protocol` | CRC-16/CCITT-FALSE, telemetry/output/power 이벤트 JSONL 빌더, 명령 11종 파서. 부동소수점 서식 미사용 |
| `sensor_app` | 모듈 통합, 2초 주기 DHT11 샘플링·텔레메트리 송출, 명령 처리, 트레일 watchdog, 사람용 `STATUS` 줄, UART 콜백 분배 |
| `main` | HAL 초기화, `SensorApp_Init()` / `SensorApp_Process()` 호출, HAL UART 콜백 전달 |

## UART 배선

```text
USART1 : Air530 GPS        9600 bps
USART2 : ZE16B-CO          9600 bps
USART3 : Jetson / 콘솔   115200 bps  (ST-LINK 가상 COM 포트)
```

## 출력 형식 — JSONL 텔레메트리 (프로토콜 v1)

기본 출력은 Jetson `gps_service.py` 파서가 검증하는 JSON 한 줄입니다. 마지막 필드는 항상
`crc16`(CRC-16/CCITT-FALSE)이며, 맞지 않는 줄은 Jetson이 버립니다.

```json
{"v":1,"event":"telemetry","seq":17,"uptime_ms":321000,"gps":{"fix":true,"lat":37.5417940,"lon":127.0795160,"sats":9,"age_s":0.5},"env":{"valid":true,"temp_c":24.3,"humidity_pct":41.0,"age_s":0.1},"co":{"valid":true,"warming_up":false,"level":"normal","alarm":false,"ppm":3,"age_s":0.2},"power":{"valid":false,"jetson_gate_on":true,"shutdown_pending":false},"crc16":"2858"}
```

**센서가 없으면 없다고 말합니다.** 값을 지어내지 않습니다 — 필드 생략으로 표현합니다.

- GPS fix가 없으면 `lat`/`lon` 자체를 내보내지 않고, 과거 fix가 있으면 `last_age_s`만 붙습니다
- DHT11 판독 실패는 `env.valid:false` + 계측값 생략
- CO 유효 프레임이 3초 넘게 없으면 `co.valid:false` + `ppm` 생략. 단 latched 경보(`level`/`alarm`)는 유지
- `co.warming_up:true` — 부팅 후 30초, ZE16B-CO 제조사 규정 예열 시간
- 미연결 하드웨어는 정직하게: `rtc` 객체 자체가 없고, `power.valid:false`(배터리 계측 없음)
- 사람용 상태 줄(`DHT11=OK,TEMP=…,ALARM=…,GATE=…`)은 `STATUS` 명령으로 즉시 출력됩니다.
  콘솔 시연 시 `STREAM OFF`로 주기 JSONL을 멈출 수 있습니다(부팅 기본은 ON)

### 명령 (USART3 수신 · 개행 종결 · 완전 일치)

| 명령 | 응답 |
|---|---|
| `PING` | `PONG` |
| `STATUS` | 사람용 상태 한 줄 |
| `GATE ON` / `GATE OFF` | `ACK GATE=…` + `event:"power"` 이벤트 |
| `STREAM ON` | 즉시 텔레메트리 1줄 (Jetson이 접속 직후 전송, 텍스트 ACK 없음) |
| `STREAM OFF` | `ACK STREAM=OFF` |
| `ALERT TRAIL ON/CAUTION/OFF` | `event:"output"` ACK — 30초 무갱신 시 watchdog 자동 off + 통지 |
| `POWER OFF ACK/CANCEL` | `event:"power"` `state:"status"` (전원 버튼 미구현 → 종료 대기 없음) |
| 그 외 / 31자 초과 | `ERR UNKNOWN_CMD` / `ERR LINE_TOO_LONG` |

스키마·명령 표 전체와 배선·Jetson 연동 절차는 프런트엔드 저장소의
[`MAP/STM32_JETSON_SETUP.md`](https://github.com/2026-ESW-OGTECH/OGTECH-frontend/blob/main/MAP/STM32_JETSON_SETUP.md) 3절이 정본입니다.

### CO 경보 규칙 (구현 완료 · 실장 검증 `[미검증]`)

| 판정 | 조건 | 출력 |
|---|---|---|
| ALARM | 100 ppm 즉시 (예열 중에도 적용 — 안전 편향) | 부저 200 ms 단속음 |
| WARN | 35 ppm이 3분 지속 | 2초마다 100 ms 비프 |
| 해제 | 30 ppm 미만이 30초 지속 | 부저 정지 |

Jetson 전원이 게이트로 차단된 상태(`GATE=OFF`)에서도 판정과 부저는 STM32에서 그대로 동작합니다 —
이것이 이 저장소의 존재 이유이며, 실장 검증(연속 20회)은 아직 남아 있습니다.

## 아직 구현하지 않은 것

목표 구조 중 **이 펌웨어에 들어 있지 않은** 항목입니다. 있는 것처럼 적지 않습니다.

| 항목 | 상태 |
|---|---|
| 진동 모터 · 스트로브 출력 | **미실장.** `ALERT TRAIL` 명령의 상태 전이·ACK·watchdog은 구현했으나 물리 출력이 없다. 경보 출력은 현재 부저(PB0)만 |
| 물리 버튼 3개(전원 / 체크포인트 / 음성) | **미구현.** `event:"button"`을 내보내지 않으며, `POWER OFF ACK/CANCEL`에는 종료 대기 없음(`state:"status"`)으로 응답 |
| `GET_TELEMETRY` · `GET_FIX` · `POWER STATUS` 폴링 명령 | **미구현** (주기 스트림 + `STREAM ON/OFF`는 구현) |
| BMP390(기압) · DS3231(RTC) | **미연결.** 텔레메트리에서 `rtc`·기압 필드 생략으로 정직하게 표현 |
| CO 급상승 판정(+20 ppm/10분) · UL 2034 다단 곡선(70/150/400 ppm) | **미구현.** 현재는 35 ppm 지속 / 100 ppm 즉시 2단 |
| 저온 경보(≤ 2 °C) | **미구현** |

온습도 센서는 이전 계획의 SHT40 대신 **DHT11**, CO 센서는 ZE07-CO가 아니라 **ZE16B-CO**입니다.

## 빌드

이 저장소에는 **사용자 코드만** 둡니다. CubeMX 생성물(`main.h`, `stm32h7xx_it.c`,
`stm32h7xx_hal_msp.c`, HAL 드라이버, `.ioc`, 링커 스크립트)은 포함하지 않습니다.
**이 저장소만 clone해서는 빌드되지 않습니다.**

1. STM32CubeIDE에서 **NUCLEO-H7A3ZI-Q**(`-Q` 접미사 디바이스) 프로젝트를 생성합니다.
2. USART1/2/3과 DHT11 데이터 핀을 설정합니다.
   핀 라벨은 반드시 **`DHT11_DATA`** 로 지정합니다 — 드라이버가 CubeMX가 만드는
   `DHT11_DATA_Pin` · `DHT11_DATA_GPIO_Port` 매크로를 그대로 씁니다.
   PB0(부저)·PC9(gate)는 모듈이 직접 초기화하므로 CubeMX에서 다른 용도로 잡지 않습니다.
3. `Core/Inc`, `Core/Src`의 모듈 **전부**(`telemetry_protocol` 포함)를 프로젝트에 넣습니다.
4. 생성된 `main.c`의 USER CODE 영역에 `SensorApp_Init()`, `SensorApp_Process()`,
   그리고 아래 콜백 전달만 추가합니다.

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) { SensorApp_UART_RxCpltCallback(huart); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)  { SensorApp_UART_ErrorCallback(huart); }
```

콘솔(USART3 115200)은 보드의 ST-LINK 가상 COM 포트로 나옵니다.

### 호스트 테스트 (하드웨어 없이)

gcc만 있으면 돌아갑니다. 계약 테스트는 나란히 체크아웃된 `OGTECH-frontend`가 필요합니다.

```bash
tests/host/run_host_tests.sh                # 프로토콜 C 단위 + Core/Src 전체 시뮬레이션
python3 tests/test_protocol_contract.py     # 시뮬레이터 출력 → 실제 Jetson 파서 왕복 검증
```

시뮬레이션은 mock `main.h`로 HAL을 대체해 `Core/Src` 전체를 한 번역 단위로 컴파일하고,
HAL 콜백 → 링 버퍼 → 파서 → 응답 경로(NMEA 문장, ZE16B 프레임, 콘솔 명령)를 실제 코드로 구동합니다.

> **재현성 남은 과제** — 제3자가 저장소만으로 빌드할 수 있도록 CubeIDE 프로젝트 전체
> (`.ioc`, `main.h`, 프로젝트 파일)를 이 저장소에 커밋하고, 현재 코드 기준 빌드 로그
> (플래시/RAM 사용량)를 남겨야 합니다. 2026-08-21 모듈 구조 + JSONL 프로토콜 기준 CubeIDE
> 빌드는 `[미검증]`입니다(호스트 gcc 컴파일·테스트는 통과).

배선과 Jetson 연동 절차는 프런트엔드 저장소의
[`MAP/STM32_JETSON_SETUP.md`](https://github.com/2026-ESW-OGTECH/OGTECH-frontend/blob/main/MAP/STM32_JETSON_SETUP.md)를 참고합니다.

## 설계 원칙

- **GPS 미수신을 추측 좌표로 채우지 않습니다.** fix 없는 좌표 필드는 내보내지 않습니다.
- **NMEA 체크섬을 검증한 문장만 받아들입니다.** 깨진 프레임으로 좌표를 만들지 않습니다.
- **CO 예열 중임을 숨기지 않습니다.** 30초 동안 `warming_up:true`·남은 초를 그대로 표시합니다.
- **센서가 끊겨도 경보는 내리지 않습니다.** 해제는 30 ppm 미만 30초 지속으로만 일어납니다.
- MQ 시리즈 가스 센서는 히터 소비전력(750 mW)이 상시 예산의 2배라 채택하지 않았습니다.
  전기화학식만 씁니다.

## 검증 상태

수치에는 근거 태그를 답니다. 하드웨어로 확인되지 않은 것을 완료로 적지 않습니다.

| 항목 | 상태 |
|---|---|
| 모듈 분리 후 동작 동일성 | 단일 파일본과 함수 18개 본문 대조 완료 `[실측: 2026-08-20]`; 경보·게이트·콘솔·프로토콜은 호스트 시뮬레이션으로 동작 확인(2026-08-21) |
| ZE16B-CO 프레임·체크섬 파싱 | 구현 완료, 호스트 시뮬레이션(프레임 주입) 통과, 실장 검증 `[미검증]` |
| CO 임계 판정·부저 경보 (PB0) | 구현 완료(2026-08-20), 호스트 시뮬레이션(100 ppm 즉시·latched·부저 펄스) 통과, 빌드·실장 `[미검증]` |
| Jetson 전원 MOSFET 게이팅 (PC9) + `GATE` 명령 | 구현 완료(2026-08-20), 호스트 시뮬레이션 통과, 빌드·실장 `[미검증]` |
| DHT11 비트뱅잉 판독 | 구현 완료, 실장 검증 `[미검증]` |
| Air530 NMEA 파싱·fix 판정 | 구현 완료, 호스트 시뮬레이션(GGA 주입·체크섬 거부) 통과, 실장 검증 `[미검증]` |
| JSONL+CRC16 텔레메트리 · `STREAM`/`ALERT TRAIL`/`POWER OFF` 명령 | 구현 완료(2026-08-21). **호스트 테스트 통과**(C 단위 + 펌웨어 시뮬레이션 + 실제 Jetson 파서 왕복 계약 8건). CubeIDE 빌드·실장 `[미검증]` |
| Jetson 측 텔레메트리 파서·CRC 테스트 | 프런트엔드 저장소에서 통과 — 펌웨어 출력과 형식 일치(위 계약 테스트로 상호 검증) |
| H7 보드 컴파일 | `[미검증]` — CubeMX 프로젝트가 이 저장소 밖에 있음 |
| Jetson 전원 OFF 상태 CO 경보 연속 20회 | `[미검증]` — 펌웨어 구현은 완료, 실장 검증이 남아 있음 |
| 상시 계층 소비전력 | `[미측정]` — 기존 0.35 W 예산은 F4 계열 기준이라 H7에서 재측정이 필요합니다 |

**하드웨어 검증 전에는 안전 장치로 간주하지 않습니다.**

## 안전 경계

OGTECH Kit은 구조 요청 수단이 아닙니다. 통신 모듈이 없으며, 조난 예방과 자력 탈출만 담당합니다.
사용자는 별도의 구조 요청 수단(휴대폰·PLB·위성 통신기)을 반드시 함께 지참해야 합니다.
