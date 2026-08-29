# OGTECH STM32 센서 허브 + Jetson UART4 적용 안내

이 디렉터리는 실제 STM32CubeMX 프로젝트에 바로 적용할 수 있는 UART4 통합 스냅샷이다.
루트 `Core/`의 JSONL+CRC16 프로토콜과 이 디렉터리의 `OGT1`+XOR 프로토콜은 서로 호환되지
않으므로, STM32의 이 코드와 `jetson/uart_receiver.py`를 한 쌍으로 사용한다.

## 확인 결론

포함된 `main.c`는 확인한 CubeMX 프로젝트 구성을 기준으로 통합한 코드다.

현재 `.ioc`에서 확인된 설정은 다음과 같다.

| 기능 | 현재 설정 | 판정 |
|---|---|---|
| MCU | STM32H7A3ZIT6Q, LQFP144 | 정상 |
| DHT11 | PA0, Output Open Drain, No Pull, 초기 High, 라벨 `DHT11_DATA` | 정상 |
| Air530 GPS | USART1, PB6 TX / PB7 RX, 9600 8N1 | 정상 |
| ZE16B-CO | USART2, PD5 TX / PD6 RX, 9600 8N1 | 정상 |
| PC 모니터 | USART3, PD8 TX / PD9 RX, 115200 8N1 | 정상 |
| Jetson | UART4, PC10 TX / PC11 RX, 115200 8N1 | 정상 |
| USART1 global interrupt | Disabled | 반드시 Enable 필요 |
| USART2 global interrupt | Disabled | 반드시 Enable 필요 |

USART1/2 인터럽트가 꺼진 채로는 `HAL_UART_Receive_IT()`를 호출해도 GPS와 CO 수신 콜백이 실행되지 않는다.

## CubeMX에서 마지막으로 고칠 것

1. `Pinout & Configuration > Connectivity > USART1 > NVIC Settings`로 이동한다.
2. `USART1 global interrupt`를 체크한다.
3. `Connectivity > USART2 > NVIC Settings`로 이동한다.
4. `USART2 global interrupt`를 체크한다.
5. 두 인터럽트의 Preemption Priority는 우선 `5`, Sub Priority는 `0`으로 둔다.
6. `GENERATE CODE`를 다시 누른다.

UART4는 현재 STM32에서 Jetson으로 송신만 하므로 global interrupt가 필요 없다. USART3도 디버그 송신만 하므로 인터럽트가 필요 없다.

## 코드 적용

`drop_in` 폴더는 현재 생성된 CubeMX 프로젝트를 기준으로 만든 완성 코드다.

- `drop_in/Core/Inc/sensor_hub.h`를 프로젝트 `Core/Inc/`로 복사
- `drop_in/Core/Inc/jetson_link.h`를 프로젝트 `Core/Inc/`로 복사
- `drop_in/Core/Src/sensor_hub.c`를 프로젝트 `Core/Src/`로 복사
- `drop_in/Core/Src/jetson_link.c`를 프로젝트 `Core/Src/`로 복사
- `drop_in/Core/Src/main.c`를 프로젝트 `Core/Src/main.c`에 적용

CubeMX에서 USART1/2 NVIC를 켜고 다시 생성했다면 `stm32h7xx_it.*`와 `stm32h7xx_hal_msp.c`는 CubeMX가 알아서 만든다. NVIC를 다시 생성하지 않고 바로 시험해야 할 때만 `drop_in`에 포함된 다음 파일도 복사한다.

- `drop_in/Core/Inc/stm32h7xx_it.h`
- `drop_in/Core/Src/stm32h7xx_it.c`
- `drop_in/Core/Src/stm32h7xx_hal_msp.c`

CubeIDE에서 프로젝트를 우클릭하고 `Refresh`, `Project > Clean`, `Build Project`를 차례로 실행한다.

## 구현된 동작

- DHT11을 2초마다 읽고 40비트 체크섬을 검증한다.
- Air530 NMEA를 인터럽트와 512바이트 링버퍼로 수신한다.
- GGA 문장의 NMEA 체크섬, Fix quality, 위성 수, 위도·경도를 검사한다.
- 유효 Fix가 5초 이상 갱신되지 않으면 `NO_FIX`로 바꾼다.
- ZE16B-CO의 9바이트 능동 출력 프레임과 체크섬을 검사한다.
- CO 센서는 부팅 후 30초 동안 `WARMING_UP` 상태로 전송한다.
- USART3으로 사람이 읽는 상태를 2초마다 출력한다.
- UART4로 Jetson 텔레메트리를 1초마다 전송한다.

UART4 프레임 형식:

```text
$OGT1,seq,uptime_ms,dht_valid,temp_x10,humidity_x10,co_state,co_ppm,gps_state,lat_e7,lon_e7,satellites*CS\r\n
```

- `co_state`: 0=WARMING_UP, 1=VALID, 2=STALE
- `gps_state`: 0=NOT_FOUND, 1=NO_FIX, 2=FIX
- `CS`: `$`와 `*` 사이 문자열의 XOR 체크섬

## Jetson 배선 및 실행

```text
STM32 PC10 (UART4_TX) -> Jetson UART RX
STM32 PC11 (UART4_RX) -> Jetson UART TX  [현재 코드는 선택 사항]
STM32 GND             -> Jetson GND
```

두 장치의 UART 신호는 3.3V TTL이어야 한다. TX와 RX는 교차하고 GND는 반드시 공통으로 연결한다. Jetson 핀 번호와 `/dev/ttyTHS*` 장치명은 Jetson 모델 및 캐리어보드에 따라 달라지므로 해당 보드 핀맵에서 확인한다.

Jetson에 `jetson/uart_receiver.py`를 복사하고 실행한다.

```bash
python3 -m pip install pyserial
python3 uart_receiver.py --self-test
python3 uart_receiver.py --port /dev/ttyTHS1 --baud 115200
```

실제 장치명이 `/dev/ttyTHS1`이 아니면 아래 결과에 나온 포트를 지정한다.

```bash
ls -l /dev/ttyTHS* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

## 검증 결과

현재 CubeMX 프로젝트 전체에 이 코드를 합친 뒤 STM32CubeIDE 2.2.0의 ARM GCC로 Clean Build했다.

```text
Build Finished. 0 errors, 0 warnings.
text=39756, data=96, bss=3296 bytes
```

이는 컴파일·링크와 HAL 인터럽트 연결이 정상이라는 뜻이다. 센서의 실제 측정값은 보드에 플래시한 뒤 USART3 출력과 Jetson 수신 결과로 최종 확인해야 한다.
