# STM32 ↔ Jetson UART 연결 및 검증 절차 (OGTECH)

> **주의:** CubeMX에서 코드를 다시 생성한 뒤에는 `SensorHub_Init()`와
> `SensorHub_Poll()` 호출이 보존됐는지 확인한다. 두 호출이 없으면 UART4 핀 설정이
> 있어도 Jetson 텔레메트리가 전송되지 않는다.

## 목표

STM32 NUCLEO-H7A3ZI-Q가 DHT11, ZE16B-CO, Air530 값을 UART4로 Jetson에 1초마다 보내고, Jetson이 체크섬을 검증해 JSON 데이터로 읽는지 확인한다.

## 확정 통신 설정

| 항목 | 설정 |
|---|---|
| STM32 주변장치 | UART4 |
| STM32 TX | PC10 / UART4_TX |
| STM32 RX | PC11 / UART4_RX |
| 통신 규격 | 115200 baud, 8 data bits, parity 없음, stop bit 1, flow control 없음 |
| 전압 | 3.3V TTL UART |

> **전기 안전:** STM32와 Jetson의 3.3V 또는 5V 전원 핀끼리는 연결하지 않는다.
> 각 장치는 자기 전원으로 켜고 **TX, RX, GND만 연결**한다. RS-232 신호나 5V UART를
> Jetson GPIO에 연결하면 안 된다.

## 1단계 — Jetson을 연결하기 전에 STM32 UART4 단독 확인

가장 안전한 방법은 3.3V USB-TTL 어댑터로 UART4 송신부터 확인하는 것이다.

```plain text
STM32 PC10 (UART4_TX) → USB-TTL RX
STM32 GND             → USB-TTL GND
USB-TTL TX            → 연결하지 않아도 됨
```

1. USB-TTL을 PC에 연결한다.
2. TeraTerm 또는 PuTTY에서 USB-TTL COM 포트를 연다.
3. 통신 설정을 115200, 8N1, flow control 없음으로 설정한다.
4. STM32를 Reset한다.
5. 1초마다 아래와 같은 프레임이 보이는지 확인한다.

```plain text
$OGT1,0,1000,1,247,910,0,0,1,0,0,0*CS
$OGT1,1,2000,1,247,910,0,0,1,0,0,0*CS
```

- 프레임이 보이면 STM32 UART4 송신은 정상이다.
- 아무것도 안 보이면 Jetson을 연결해도 수신되지 않는다. `MX_UART4_Init()`, `SensorHub_Init()`, `SensorHub_Poll()`, `JetsonLink_Send()` 호출부터 확인한다.
- USART3의 `DHT11=OK,...` 출력은 PC 디버그용이며 UART4 송신 정상 여부를 증명하지 않는다.

## 2단계 — Jetson에 연결

UART는 TX와 RX를 교차 연결하고 GND를 공통으로 연결한다.

```plain text
STM32 PC10 (UART4_TX) → Jetson UART RX
STM32 PC11 (UART4_RX) ← Jetson UART TX
STM32 GND             → Jetson GND
```

현재 펌웨어는 STM32에서 Jetson으로 보내는 기능이 중심이므로 최초 시험에서는 PC10→Jetson RX와 GND만 연결해도 된다. 양방향 명령 기능을 추가할 때 PC11도 연결한다.

### Xavier NX 개발키트 P3509의 J12 40핀 헤더 예시

```plain text
STM32 PC10 (TX) → J12 pin 10 (UART1_RXD)
STM32 PC11 (RX) ← J12 pin 8  (UART1_TXD)
STM32 GND       → J12 pin 6  (GND)
```

Jetson 모델이나 커스텀 캐리어보드가 다르면 위 핀 번호를 그대로 사용하지 않는다. 해당 캐리어보드 핀맵에서 **3.3V UART TX/RX/GND**를 확인한다. NVIDIA의 [Jetson 시작 및 하드웨어 문서](https://developer.nvidia.com/embedded/learn/getting-started-jetson)와 [Jetson-IO 안내](https://docs.nvidia.com/jetson/l4t/Tegra%20Linux%20Driver%20Package%20Development%20Guide/hw_setup_jetson_io.html)를 참고한다.

## 3단계 — Jetson UART 장치 확인

Jetson 터미널에서 실행한다.

```bash
dmesg | grep -E 'ttyTHS|ttyS|serial'
ls -l /dev/ttyTHS* /dev/ttyS* 2>/dev/null
```

Xavier NX 개발키트의 J12 pin 8/10 일반 UART는 환경에 따라 `/dev/ttyTHS0` 등으로 나타날 수 있다. 장치명은 JetPack, DTB, 캐리어보드에 따라 달라질 수 있으므로 명령 결과로 확정한다.

핀 기능이 UART로 활성화되지 않았다면 다음 도구에서 40핀 헤더 UART 설정을 확인하고 재부팅한다.

```bash
sudo /opt/nvidia/jetson-io/jetson-io.py
```

포트 접근 권한이 없으면 사용자를 `dialout` 그룹에 추가하고 로그아웃 후 다시 로그인한다.

```bash
sudo usermod -aG dialout "$USER"
```

포트를 콘솔 서비스가 사용 중인지 먼저 확인한다.

```bash
systemctl status nvgetty
```

해당 서비스가 **실제로 사용할 UART 포트를 점유할 때만** 다음처럼 중지하고 비활성화한다.

```bash
sudo systemctl stop nvgetty
sudo systemctl disable nvgetty
```

## 4단계 — Jetson UART 자체 Loopback 시험

STM32를 분리한 상태에서 Jetson UART TX와 RX를 임시로 서로 연결한다. Xavier NX J12 예시에서는 pin 8과 pin 10을 점퍼로 연결한다.

```bash
python3 -m pip install pyserial
python3 - <<'PY'
import serial
port = "/dev/ttyTHS0"  # 실제 확인한 장치명으로 변경
with serial.Serial(port, 115200, timeout=1) as uart:
    uart.reset_input_buffer()
    uart.write(b"JETSON_LOOPBACK\n")
    print(uart.readline())
PY
```

`b'JETSON_LOOPBACK\n'`이 돌아오면 Jetson 포트, baudrate, pinmux가 정상이다. 시험 후 TX-RX 점퍼를 반드시 제거한다.

## 5단계 — STM32 텔레메트리 실제 수신

프로젝트의 `uart_receiver.py`를 Jetson으로 복사한 뒤 실행한다.

```bash
python3 -m pip install pyserial
python3 uart_receiver.py --self-test
python3 uart_receiver.py --port /dev/ttyTHS0 --baud 115200
```

정상 출력 예시:

```json
{"sequence": 7, "uptime_ms": 12345, "dht_valid": true, "temperature_c": 24.7, "humidity_percent": 91.0, "co_state": "VALID", "co_ppm": 0, "gps_state": "NO_FIX", "latitude": null, "longitude": null, "satellites": 0}
```

이 JSON이 1초마다 나오고 `sequence`가 1씩 증가하면 STM32→Jetson UART 통신은 정상이다.

## 프레임 의미

```plain text
$OGT1,seq,uptime_ms,dht_valid,temp_x10,humidity_x10,co_state,co_ppm,gps_state,lat_e7,lon_e7,satellites*CS
```

- `co_state`: 0=WARMING_UP, 1=VALID, 2=STALE
- `gps_state`: 0=NOT_FOUND, 1=NO_FIX, 2=FIX
- `temp_x10=247`: 24.7°C
- `humidity_x10=910`: 91.0%
- `lat_e7/lon_e7`: 위도·경도 × 10,000,000
- `CS`: 프레임 손상 검사용 XOR 체크섬

## 문제별 진단

| 증상 | 우선 확인할 것 |
|---|---|
| Jetson에 아무것도 안 들어옴 | STM32 UART4 송신 코드, PC10→Jetson RX, 공통 GND, Jetson 장치명, pinmux, nvgetty 점유 |
| 깨진 문자 출력 | 양쪽 115200 8N1 일치, 공통 GND, 3.3V 레벨 |
| 체크섬 오류가 반복됨 | 배선 길이, 접촉 불량, GND, 전원 노이즈, 잘못된 baudrate |
| JSON은 정상인데 GPS가 NOT_FOUND | UART4 통신은 정상이며 GPS USART1 문제를 별도로 점검 |
| USART3에는 출력되지만 Jetson에는 없음 | USART3와 UART4는 별개이므로 PC10 UART4 송신 코드 확인 |

## 최종 체크리스트

- [ ] STM32 CubeMX에서 UART4 PC10/PC11, 115200 8N1 확인
- [ ] STM32 펌웨어에 `SensorHub_Init()`와 `SensorHub_Poll()` 포함
- [ ] USB-TTL로 PC10에서 `$OGT1,...*CS` 프레임 확인
- [ ] Jetson UART가 3.3V인지 확인
- [ ] TX↔RX 교차 연결
- [ ] GND 공통 연결
- [ ] Jetson 실제 `/dev/ttyTHS*` 장치명 확인
- [ ] Jetson loopback 통과
- [ ] `uart_receiver.py --self-test` 통과
- [ ] 실제 JSON 수신 및 sequence 증가 확인
- [ ] 10분 이상 연속 수신하여 DROP/체크섬 오류 확인
