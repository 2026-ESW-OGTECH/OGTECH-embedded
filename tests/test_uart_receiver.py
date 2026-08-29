from pathlib import Path
import importlib.util
import sys
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / "jetson" / "uart_receiver.py"
SPEC = importlib.util.spec_from_file_location("uart_receiver", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
uart_receiver = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = uart_receiver
SPEC.loader.exec_module(uart_receiver)


def make_frame(body: str) -> str:
    return f"${body}*{uart_receiver.xor_checksum(body):02X}\r\n"


class UartReceiverTest(unittest.TestCase):
    def test_parse_valid_ogtech_frame(self) -> None:
        body = "OGT1,7,12345,1,234,567,1,12,2,375465126,1270757141,9"
        telemetry = uart_receiver.parse_frame(make_frame(body))

        self.assertEqual(telemetry.sequence, 7)
        self.assertEqual(telemetry.temperature_c, 23.4)
        self.assertEqual(telemetry.co_ppm, 12)
        self.assertEqual(telemetry.latitude, 37.5465126)

    def test_rejects_checksum_mismatch(self) -> None:
        with self.assertRaisesRegex(uart_receiver.FrameError, "checksum mismatch"):
            uart_receiver.parse_frame("$OGT1,1,2,0,0,0,0,0,0,0,0,0*00")

    def test_rejects_unknown_protocol_marker(self) -> None:
        body = "BAD1,7,12345,1,234,567,1,12,2,375465126,1270757141,9"
        with self.assertRaisesRegex(uart_receiver.FrameError, "unsupported protocol"):
            uart_receiver.parse_frame(make_frame(body))


if __name__ == "__main__":
    unittest.main()
