# STM32 Smart Tray Controller

SafeAid Kit의 Jetson-STM32 하드웨어 제어용 STM32 펌웨어 초안입니다. Jetson 앱은 USB Serial/UART로 명령을 보내고, STM32F401RET6는 DM-S1300MD 서보, 구역 LED, 재고 센서, 배터리 전압, 이후 SIM7670G AT command 처리를 담당합니다.

## Serial Protocol

Baud rate: `115200`

```text
OPEN_LAYER 1
OPEN_LAYER 2
OPEN_LAYER 3
SET_CELL_LED 2-3
CLOSE_ALL
READ_STOCK
GET_BATTERY
```

응답은 한 줄 JSON 형태입니다.

```json
{"ok":true,"event":"open_layer","layer":2}
```

## Pin Map

실제 보드와 배선에 맞게 `.ino` 상단 배열을 수정하세요.

- `SERVO_PINS`: DM-S1300MD 서보 4개. 현재 `OPEN_LAYER`는 앞의 3개를 층 구동에 사용하고, 4번째는 상부 뚜껑/보조 잠금/예비 액추에이터 후보입니다.
- `CELL_LED_PINS`: 현재 LED 배치 10구역. `1-1`~`1-3`, `2-1`~`2-6`, `3-1`.
- `STOCK_SENSOR_PINS`: LED 구역과 같은 10구역의 재고/존재 감지 센서.
- `BATTERY_ADC_PIN`: 4S LiFePO4 `BAT_IN` 분압 입력.

전원은 `5V_COMPUTE`, `5V_LED`, `6V_SERVO`, `3V3_LOGIC`, `BAT_SIM`으로 분리합니다. 서보 전원은 STM32 보드에서 공급하지 않고 별도 6V rail을 사용하며, GND만 star point 기준으로 공통화합니다.

## Power Assumptions

- Battery/main input: 4S LiFePO4, `BAT_IN` 10V~14.6V
- Logic rail: 3.3V
- Servo rail: 6V, peak 12A~15A
- LED rail: 5V, 실제 총 길이 기준으로 전류 계산
- Jetson/LCD/USB audio rail: 5V_COMPUTE, LED/서보 부하와 분리

서보 rail에는 fuse/eFuse, TVS, bulk capacitor, 각 커넥터 근처 capacitor를 넣고, PWM line에는 series resistor와 pulldown을 둡니다.
