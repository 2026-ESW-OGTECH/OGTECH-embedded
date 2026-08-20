# OGTECH-embedded — STM32 상시 계층 펌웨어

**SafeAid Kit** (2026 임베디드 소프트웨어 경진대회 자유공모 / 팀 OGTECH) 의 펌웨어 저장소입니다.
[조직 개요](https://github.com/2026-ESCW-OGTECH) · [다른 저장소 안내](https://github.com/2026-ESCW-OGTECH/.github)

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

## 구성

```text
Core/
├─ Inc/
│  ├─ air530_gps.h      GPS 드라이버 인터페이스
│  ├─ dht11.h           온습도 드라이버 인터페이스
│  ├─ ze16b_co.h        CO 드라이버 인터페이스
│  └─ sensor_app.h      통합 계층 인터페이스
└─ Src/
   ├─ air530_gps.c      NMEA 파싱 · 링 버퍼 · 좌표 변환      (411줄)
   ├─ dht11.c           비트뱅잉 판독 · DWT 마이크로초 지연   (136줄)
   ├─ ze16b_co.c        9바이트 프레임 · 체크섬 · ppm         (132줄)
   ├─ sensor_app.c      세 드라이버 통합 · 주기 · 상태 출력   (181줄)
   └─ main.c            CubeMX/HAL 초기화와 진입점            (313줄)
```

**드라이버는 MCU 의존 코드에서 분리돼 있습니다.** 보드를 옮길 때 `main.c`만 CubeMX로 다시 생성하고
드라이버 4쌍은 그대로 이식합니다.

| 모듈 | 하는 일 |
|---|---|
| `dht11` | GPIO 모드 전환, DWT 사이클 카운터 기반 µs 지연, 40비트 수신, 체크섬 검증 |
| `air530_gps` | UART RX 인터럽트 바이트 버퍼링(256 B 링), NMEA 체크섬 검증, GGA 파싱, `도 × 1e7` 변환, fix·위성 수 관리 |
| `ze16b_co` | UART RX 인터럽트 바이트 버퍼링(64 B 링), 9바이트 프레임 동기화·체크섬, ppm 산출 |
| `sensor_app` | 세 모듈 통합, 2초 주기 DHT11 샘플링, GPS·CO 백그라운드 처리, 상태 문자열 출력, UART 콜백 분배 |
| `main` | HAL 초기화, `SensorApp_Init()` / `SensorApp_Process()` 호출, HAL UART 콜백 전달 |

## UART 배선

```text
USART1 : Air530 GPS    9600 bps
USART2 : ZE16B-CO      9600 bps
USART3 : 상태 콘솔   115200 bps
```

## 동작

```c
if (SensorApp_Init(&huart1, &huart2, &huart3) != HAL_OK) { Error_Handler(); }

while (1) { SensorApp_Process(); }
```

`SensorApp_Process()`는 매 루프마다 GPS·CO 링 버퍼를 비우고, **2초마다** DHT11을 한 번 읽은 뒤
상태 한 줄을 출력합니다. DHT11 판독은 20 ms 넘게 블로킹하므로, 그동안 인터럽트로 쌓인 UART
바이트를 판독 직후 다시 배수한 뒤 출력합니다.

### 상태 출력 형식

```text
DHT11=OK,TEMP=24.0C,HUM=41.0%,CO=0ppm,GPS=FIX,LAT=37.5417940,LON=127.0795160,SAT=9
DHT11=ERROR,CO=WARMING_UP(18s),GPS=NOT_FOUND
```

**센서가 없으면 없다고 말합니다.** 값을 지어내지 않습니다.

- `CO=WARMING_UP(n s)` — 부팅 후 30초. ZE16B-CO 제조사 규정 예열 시간입니다.
- `CO=NOT_FOUND` — 유효 프레임이 3초 넘게 없을 때
- `GPS=NOT_FOUND` — NMEA가 5초 넘게 없을 때 · `GPS=NO_FIX` — 수신은 되나 fix 없음
- `DHT11=ERROR` — 판독 실패 또는 체크섬 불일치

## 아직 구현하지 않은 것

목표 구조 중 **이 펌웨어에 들어 있지 않은** 항목입니다. 있는 것처럼 적지 않습니다.

| 항목 | 상태 |
|---|---|
| CO 임계 판정과 부저·진동·스트로브 출력 | **미구현.** 현재는 ppm 판독·출력까지 |
| Jetson 전원 게이팅(MOSFET) 제어 | **미구현** |
| 물리 버튼 3개(전원 / 체크포인트 / 음성) | **미구현** |
| Jetson용 JSONL 텔레메트리 + CRC16 | **미구현.** 현재 출력은 사람이 읽는 콘솔 형식 |
| `STREAM` · `GET_TELEMETRY` · `GET_FIX` · `PING` 명령 | **미구현** |
| BMP390(기압) · DS3231(RTC) | **미연결** |

온습도 센서는 이전 계획의 SHT40 대신 **DHT11**, CO 센서는 ZE07-CO가 아니라 **ZE16B-CO**입니다.

## 빌드

이 저장소에는 **사용자 코드만** 둡니다. CubeMX 생성물(`main.h`, `stm32h7xx_it.c`,
`stm32h7xx_hal_msp.c`, HAL 드라이버, `.ioc`, 링커 스크립트)은 포함하지 않습니다.
**이 저장소만 clone해서는 빌드되지 않습니다.**

1. STM32CubeIDE에서 대상 H7 보드용 프로젝트를 생성합니다.
2. USART1/2/3과 DHT11 데이터 핀을 설정합니다.
   핀 라벨은 반드시 **`DHT11_DATA`** 로 지정합니다 — 드라이버가 CubeMX가 만드는
   `DHT11_DATA_Pin` · `DHT11_DATA_GPIO_Port` 매크로를 그대로 씁니다.
3. `Core/Inc`, `Core/Src`의 드라이버 4쌍을 프로젝트에 넣습니다.
4. 생성된 `main.c`의 USER CODE 영역에 `SensorApp_Init()`, `SensorApp_Process()`,
   그리고 아래 콜백 전달만 추가합니다.

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) { SensorApp_UART_RxCpltCallback(huart); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)  { SensorApp_UART_ErrorCallback(huart); }
```

배선과 Jetson 연동 절차는 프런트엔드 저장소의
[`MAP/STM32_JETSON_SETUP.md`](https://github.com/2026-ESCW-OGTECH/OGTECH-frontend/blob/main/MAP/STM32_JETSON_SETUP.md)를 참고합니다.

## 설계 원칙

- **GPS 미수신을 추측 좌표로 채우지 않습니다.** `GPS=NOT_FOUND`를 그대로 내보냅니다.
- **NMEA 체크섬을 검증한 문장만 받아들입니다.** 깨진 프레임으로 좌표를 만들지 않습니다.
- **CO 예열 중임을 숨기지 않습니다.** 30초 동안은 남은 초를 그대로 표시합니다.
- MQ 시리즈 가스 센서는 히터 소비전력(750 mW) 때문에 채택하지 않았습니다. 전기화학식만 씁니다.

## 검증 상태

수치에는 근거 태그를 답니다. 하드웨어로 확인되지 않은 것을 완료로 적지 않습니다.

| 항목 | 상태 |
|---|---|
| 모듈 분리 후 동작 동일성 | 단일 파일본과 함수 18개 본문 대조 완료 `[실측: 2026-08-20]` |
| ZE16B-CO 프레임·체크섬 파싱 | 구현 완료, 실장 검증 `[미검증]` |
| DHT11 비트뱅잉 판독 | 구현 완료, 실장 검증 `[미검증]` |
| Air530 NMEA 파싱·fix 판정 | 구현 완료, 실장 검증 `[미검증]` |
| H7 보드 컴파일 | `[미검증]` — CubeMX 프로젝트가 이 저장소 밖에 있음 |
| Jetson 전원 OFF 상태 CO 경보 연속 20회 | `[미검증]` — 경보 출력이 아직 미구현 |
| 상시 계층 소비전력 | `[미측정]` — 기존 0.35 W 예산은 F4 계열 기준이라 H7에서 재측정이 필요합니다 |

**하드웨어 검증 전에는 안전 장치로 간주하지 않습니다.**

## 안전 경계

SafeAid Kit은 구조 요청 수단이 아닙니다. 통신 모듈이 없으며, 조난 예방과 자력 탈출만 담당합니다.
사용자는 별도의 구조 요청 수단(휴대폰·PLB·위성 통신기)을 반드시 함께 지참해야 합니다.
