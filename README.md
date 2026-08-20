# SafeAid Kit Embedded

STM32F401RET6 상시 전원 관리자·센서 허브·GNSS 로거 펌웨어 저장소다.

현재 `stm32_smart_tray_controller/` 폴더 이름은 Git 이력 때문에 유지하지만, 내용은 구 트레이 제어 코드를
제거하고 다음 P0 기능으로 교체했다.

- Air530 UART/NMEA 수신
- SHT40 I2C 온·습도 수신
- ZE07-CO UART 프레임·체크섬·ppm 판독
- CO 임계 판정과 부저·진동·스트로브 **STM32 단독 출력**
- Jetson용 1 Hz JSONL 텔레메트리와 CRC16
- `STREAM`, `GET_TELEMETRY`, `GET_FIX`, `PING` 명령

Jetson 전원이 꺼져 있어도 CO 물리 경보는 계속된다. GPS 미수신을 추측 좌표로 채우지 않으며, Air530
HDOP를 미터 정확도로 임의 변환하지 않는다.

배선·빌드·검증 절차는 프런트엔드 MAP의
[`STM32_JETSON_SETUP.md`](../OGTECH-frontend/MAP/STM32_JETSON_SETUP.md)를 따른다.

## 검증 상태

- 프런트엔드 Python 텔레메트리 파서·CRC 테스트: 구현 완료
- Arduino CLI + STM32duino 3.0.0 Nucleo-F401RE 컴파일: 성공 `[실측: 2026-08-09]`
- 플래시 44,400 B(8%), 전역 RAM 5,228 B(5%) `[실측: 2026-08-09]`
- 실제 Air530/SHT40/ZE07-CO 결선: `[미검증]`
- Jetson 전원 OFF 상태 CO 경보 20회: `[미검증]`

하드웨어 검증 전에는 안전 장치로 간주하지 않는다.
