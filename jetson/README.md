# OGTECH Jetson UART receiver

`uart_receiver.py`는 STM32 UART4에서 전송한 `OGT1` 텔레메트리 프레임을 읽고 XOR
체크섬과 필드 범위를 검증한 뒤 JSON으로 출력한다. UI 코드는 포함하지 않는다.

```bash
python3 -m pip install -r requirements.txt
python3 uart_receiver.py --self-test
python3 uart_receiver.py --port /dev/ttyTHS0 --baud 115200
```

프레임 형식과 배선·검증 절차는 `../uart4_integration/VERIFICATION.md`를 참고한다.
