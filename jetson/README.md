# OGTECH Jetson UART receiver

`uart_receiver.py`는 구 펌웨어(`uart4_integration`, 보드 실장본 `$SA1`)가 UART4로 보내는 `OGT1`/`SA1` 텔레메트리
프레임을 읽고 XOR 체크섬과 필드 범위를 검증한 뒤 JSON으로 출력하는 **진단 도구**다. UI 코드는 포함하지 않는다.
정본 펌웨어(`Core/`, JSONL v1 · UART4)의 수신은 OGTECH-frontend `MAP/gps_service.py`(`app.py --gps-mode stm32 --gps-port /dev/ttyTHS0`)가 맡는다.

```bash
python3 -m pip install -r requirements.txt
python3 uart_receiver.py --self-test
python3 uart_receiver.py --port /dev/ttyTHS0 --baud 115200   # 기본값도 /dev/ttyTHS0
```

프레임 형식과 배선·검증 절차는 `../uart4_integration/VERIFICATION.md`를 참고한다.
