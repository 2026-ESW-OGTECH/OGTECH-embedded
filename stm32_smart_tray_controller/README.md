# STM32F401RE 상시 센서 허브

> 폴더 이름은 구 도메인 이력 때문에 유지한다. 현재 펌웨어에는 서보·트레이·재고 센서 기능이 없다.

## 핀 배치

| 인터페이스 | STM32 핀 | 장치 |
|---|---|---|
| USART1 RX/TX | PA10 / PA9 | Air530, 9600 8N1 |
| USART6 RX/TX | PC7 / PC6 | ZE07-CO, 9600 8N1 |
| USART2 RX/TX | PA3 / PA2 | Jetson, 115200 8N1 |
| I2C1 SDA/SCL | PB9 / PB8 | SHT40 `0x44` |
| GPIO | PB0 / PB1 / PC8 | 부저 / 진동 / 스트로브 드라이버 |

GPIO는 부하를 직접 구동하지 않는다. 트랜지스터/MOSFET, 저항, 모터 역기전력 다이오드를 사용한다.

## 텔레메트리

1초마다 GPS·환경·CO·전원 자리표시자를 한 줄 JSON으로 전송한다 `[출처: 펌웨어]`. 마지막 `crc16`은
그 필드를 붙이기 전 JSON 전체에 대한 CRC-16/CCITT-FALSE다.

```json
{"v":1,"event":"telemetry","seq":1,"uptime_ms":1000,"gps":{"fix":false,"sats":0,"hdop":null,"acc_m":null,"age_s":null,"last_age_s":null},"env":{"valid":true,"temp_c":23.45,"humidity_pct":58.20,"age_s":0.1},"co":{"valid":false,"warming_up":true,"ppm":3.2,"level":"normal","alarm":false,"age_s":0.1},"power":{"valid":false,"percent":null,"days_left":null},"crc16":"0000"}
```

예시의 CRC `0000`은 형식 자리표시자이며 실제 출력값이 아니다.

## CO 경보

- 주의: 35 ppm 3분 또는 최근 10분 최저 대비 +20 ppm `[추정: 프로젝트 기준]`
- 경보: 100 ppm 즉시, 또는 70 ppm/60분·150 ppm/10분·400 ppm/4분 `[출처: 프로젝트 기준]`
- 해제: 30 ppm 미만 30초 `[추정: 래치 해제 기준]`
- 첫 5분: 화면 상태는 예열. 단, 100 ppm 이상은 물리 경보 `[추정: 안전 편향]`

이미 발생한 경보는 CO 직렬 입력이 끊겼다는 이유만으로 해제하지 않는다.

## 명령

```text
PING
STREAM ON
STREAM OFF
GET_TELEMETRY
GET_FIX
```

`GET_FIX`는 기존 Jetson 파서 호환용이다. fix가 없으면 마지막 좌표 경과 시간만 보내고 좌표를 추정하지 않는다.

자세한 결선과 Jetson 실행은
[`STM32_JETSON_SETUP.md`](../../smartaid-frontend/MAP/STM32_JETSON_SETUP.md)를 따른다.

## 빌드 확인

```bash
arduino-cli compile \
  --fqbn STMicroelectronics:stm32:Nucleo_64:pnum=NUCLEO_F401RE \
  stm32_smart_tray_controller
```

STM32duino 3.0.0에서 성공했고 플래시 44,400 B(8%), 전역 RAM 5,228 B(5%)를 사용했다
`[실측: 2026-08-09]`.
