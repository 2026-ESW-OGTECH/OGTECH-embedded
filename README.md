# OGTECH-embedded — STM32 상시 계층 펌웨어

**SafeAid Kit** (2026 임베디드 소프트웨어 경진대회 자유공모 / 팀 OGTECH) 의 펌웨어 저장소입니다.
[조직 개요](https://github.com/2026-ESCW-OGTECH) · [다른 저장소 안내](https://github.com/2026-ESCW-OGTECH/.github)

---

## 이 저장소가 하는 일 한 줄

**Jetson 전원이 꺼져 있어도 일산화탄소를 감시하고 경보를 울린다.**

밀폐된 텐트 안의 연소기구는 수면 중에 초기 증상을 자각할 수 없다는 점이 가장 위험합니다.
그래서 감시 주체를 Jetson(18 W)이 아니라 STM32(0.35 W)로 내렸습니다.
STM32는 Jetson의 전원을 MOSFET로 물리 차단한 채 상시 동작하며, CO 임계를 넘으면
**Jetson 부팅을 기다리지 않고 자기가 직접** 부저·진동·스트로브를 켭니다.

이 구조가 이 작품의 해자입니다. 지도 앱으로는 흉내 낼 수 없고, 14일 운용도 여기서만 성립합니다.

## 구성

```text
stm32_smart_tray_controller/
├─ stm32_smart_tray_controller.ino   상시 계층 펌웨어 본체
└─ README.md                         핀 배치와 명령 프로토콜
```

> 폴더 이름 `stm32_smart_tray_controller`는 Git 이력 연속성 때문에 유지합니다.
> 내용은 구 트레이 제어 코드를 전부 걷어내고 아래 상시 계층 기능으로 교체했습니다.

## 구현 기능

| 기능 | 내용 |
|---|---|
| GNSS | Air530 UART/NMEA 수신, fix 판정 |
| 환경 | SHT40 I2C 온·습도 수신 |
| 안전 | ZE07-CO UART 프레임·체크섬 검증과 ppm 판독 |
| 경보 | CO 임계 판정 후 부저·진동·스트로브 **STM32 단독 출력** |
| 텔레메트리 | Jetson용 1 Hz JSONL + CRC16 |
| 명령 | `STREAM` · `GET_TELEMETRY` · `GET_FIX` · `PING` |

## 설계 원칙

- **GPS 미수신을 추측 좌표로 채우지 않습니다.** 마지막 확정 좌표와 경과 시간만 넘깁니다.
- **Air530 HDOP를 미터 정확도로 임의 변환하지 않습니다.** 근거 없는 숫자를 만들지 않기 위해서입니다.
- **CO 경보는 어떤 전원 상태에서도 최우선입니다.** Jetson 상태와 무관하게 동작합니다.
- MQ 시리즈 가스 센서는 히터 소비전력(750 mW)이 상시 예산의 2배라 채택하지 않았습니다.

## 빌드와 배선

Arduino CLI 또는 PlatformIO + STM32duino, 보드는 Nucleo-F401RE입니다.
배선·플래시·검증 절차는 프런트엔드 저장소의
[`MAP/STM32_JETSON_SETUP.md`](https://github.com/2026-ESCW-OGTECH/OGTECH-frontend/blob/main/MAP/STM32_JETSON_SETUP.md)를 따릅니다.

## 검증 상태

수치에는 근거 태그를 답니다. 하드웨어로 확인되지 않은 것을 완료로 적지 않습니다.

| 항목 | 상태 |
|---|---|
| Arduino CLI + STM32duino 3.0.0 Nucleo-F401RE 컴파일 | 성공 `[실측: 2026-08-09]` |
| 플래시 사용량 | 44,400 B (8%) `[실측: 2026-08-09]` |
| 전역 RAM 사용량 | 5,228 B (5%) `[실측: 2026-08-09]` |
| Jetson 측 텔레메트리 파서·CRC 테스트 | 통과 (프런트엔드 저장소) |
| 실제 Air530 / SHT40 / ZE07-CO 결선 | `[미검증]` |
| Jetson 전원 OFF 상태 CO 경보 연속 20회 | `[미검증]` |

**하드웨어 검증 전에는 안전 장치로 간주하지 않습니다.**

## 안전 경계

SafeAid Kit은 구조 요청 수단이 아닙니다. 통신 모듈이 없으며, 조난 예방과 자력 탈출만 담당합니다.
능동 신호는 부저·스트로브 6회/분 반복까지입니다.
