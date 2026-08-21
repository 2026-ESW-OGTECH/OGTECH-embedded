"""펌웨어 ↔ Jetson 프로토콜 계약 테스트.

호스트 시뮬레이터(tests/host/test_firmware_sim --emit)가 실제 펌웨어 코드로
만들어 낸 USART3 출력을, 프런트엔드 저장소의 실제 파서(MAP/gps_service.py)에
그대로 먹여 왕복을 검증한다. 양쪽 모두 사본이 아니라 실물 코드다.

실행: python3 tests/test_protocol_contract.py
전제: gcc, 그리고 이 저장소와 나란히 체크아웃된 OGTECH-frontend.
"""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import unittest

EMBEDDED_ROOT = Path(__file__).resolve().parents[1]
HOST_DIR = EMBEDDED_ROOT / "tests" / "host"
FRONTEND_MAP = EMBEDDED_ROOT.parent / "OGTECH-frontend" / "MAP"


def _load_gps_service():
    sys.path.insert(0, str(FRONTEND_MAP))
    try:
        import gps_service
    finally:
        sys.path.pop(0)
    return gps_service


def _emit_lines() -> list[str]:
    subprocess.run(
        ["sh", str(HOST_DIR / "run_host_tests.sh")],
        check=True,
        capture_output=True,
        text=True,
    )
    result = subprocess.run(
        [str(HOST_DIR / "build" / "test_firmware_sim"), "--emit"],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


@unittest.skipIf(shutil.which("gcc") is None, "gcc가 없어 호스트 빌드 불가")
@unittest.skipIf(not FRONTEND_MAP.is_dir(), "OGTECH-frontend가 나란히 없음")
class ProtocolContractTest(unittest.TestCase):
    """시뮬레이터 출력 전 줄을 GpsService와 같은 우선순위로 파싱한다."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.gps = _load_gps_service()
        cls.lines = _emit_lines()

    def _parse_like_service(self, line: str):
        """gps_service._handle_line(stm32 모드)과 같은 순서로 시도한다."""
        gps = self.gps
        telemetry = gps.parse_stm32_telemetry(line)
        if telemetry is not None:
            return "telemetry", telemetry
        button = gps.parse_stm32_button(line)
        if button is not None:
            return "button", button
        output = gps.parse_stm32_output(line)
        if output is not None:
            return "output", output
        power = gps.parse_stm32_power_event(line)
        if power is not None:
            return "power", power
        return "fix", gps.parse_stm32_fix(line)

    def _accepted(self):
        accepted = []
        for line in self.lines:
            if not line.startswith("{"):
                continue
            kind, parsed = self._parse_like_service(line)
            accepted.append((kind, parsed, line))
        return accepted

    def test_every_json_line_is_accepted(self) -> None:
        accepted = self._accepted()
        self.assertGreaterEqual(len(accepted), 12)
        for kind, parsed, line in accepted:
            self.assertIn(kind, {"telemetry", "output", "power"}, line)
            self.assertIsNotNone(parsed, line)

    def test_human_lines_are_rejected_like_service(self) -> None:
        """사람용 ACK 줄은 JSON이 아니므로 서비스가 거부(카운트)한다."""
        human = [line for line in self.lines if not line.startswith("{")]
        self.assertTrue(all(line.startswith("ACK ") for line in human), human)
        for line in human:
            with self.assertRaises(self.gps.GpsInputError):
                self._parse_like_service(line)

    def test_telemetry_values_roundtrip(self) -> None:
        telemetry = [p for k, p, _ in self._accepted() if k == "telemetry"]
        self.assertGreaterEqual(len(telemetry), 6)

        # 시나리오 2: 전 센서 정상 + fix (emit 순서 기준 두 번째 텔레메트리)
        full = telemetry[1]
        self.assertTrue(full["gps"]["fix"])
        self.assertAlmostEqual(full["gps"]["lat"], 37.5417940, places=7)
        self.assertAlmostEqual(full["gps"]["lon"], 127.0795160, places=7)
        self.assertEqual(full["gps"]["satellites"], 9)
        self.assertAlmostEqual(full["environment"]["temp_c"], 24.3)
        self.assertAlmostEqual(full["environment"]["humidity_pct"], 41.0)
        self.assertFalse(full["environment"]["pressure_valid"])
        self.assertEqual(full["co"]["level"], "normal")
        self.assertAlmostEqual(full["co"]["ppm"], 3.0)
        self.assertFalse(full["rtc"]["valid"])
        self.assertFalse(full["power"]["valid"])
        self.assertTrue(full["power"]["jetson_gate_on"])
        self.assertFalse(full["power"]["shutdown_pending"])

        # 시나리오 3: CO 즉시 경보
        alarm = telemetry[2]
        self.assertEqual(alarm["co"]["level"], "alarm")
        self.assertTrue(alarm["co"]["alarm"])
        self.assertAlmostEqual(alarm["co"]["ppm"], 150.0)

        # 시나리오 4: WARN latched + 센서 단절 — 값 없이 경보만 유지
        stale = telemetry[3]
        self.assertFalse(stale["co"]["valid"])
        self.assertIsNone(stale["co"]["ppm"])
        self.assertEqual(stale["co"]["level"], "warning")

        # 시나리오 5: fix 상실 후 last_age 보고, 좌표 없음
        lost = telemetry[4]
        self.assertFalse(lost["gps"]["fix"])
        self.assertNotIn("lat", lost["gps"])
        self.assertAlmostEqual(lost["gps"]["last_age_s"], 12.0)

        # gate off 반영 텔레메트리가 존재해야 한다 (시나리오 8)
        self.assertTrue(
            any(t["power"]["jetson_gate_on"] is False for t in telemetry)
        )

    def test_telemetry_sequence_is_consecutive(self) -> None:
        """Jetson의 sequence_gaps 판정 기준: 텔레메트리 seq는 줄마다 +1."""
        seqs = [p["sequence"] for k, p, _ in self._accepted() if k == "telemetry"]
        self.assertEqual(seqs, list(range(seqs[0], seqs[0] + len(seqs))))

    def test_output_acks_roundtrip(self) -> None:
        outputs = [p for k, p, _ in self._accepted() if k == "output"]
        self.assertEqual(
            [o["level"] for o in outputs], ["alert", "caution", "off"]
        )
        for o in outputs:
            self.assertEqual(o["output"], "trail")
            self.assertEqual(o["watchdog_ms"], 30000)
            self.assertEqual(o["active"], o["level"] != "off")

    def test_power_events_roundtrip(self) -> None:
        powers = [p for k, p, _ in self._accepted() if k == "power"]
        self.assertEqual(
            [p["state"] for p in powers],
            ["gate_off", "gate_on", "status", "status"],
        )
        self.assertFalse(powers[0]["gate_on"])
        self.assertTrue(powers[1]["gate_on"])
        self.assertTrue(all(p["shutdown_pending"] is False for p in powers))

    def test_corrupted_byte_is_rejected_by_crc(self) -> None:
        line = next(line for line in self.lines if '"event":"telemetry"' in line)
        # 페이로드 내부 숫자 하나를 바꾸면 JSON은 유효해도 CRC가 거부해야 한다.
        corrupted = line.replace('"uptime_ms":2', '"uptime_ms":3', 1)
        self.assertNotEqual(line, corrupted)
        with self.assertRaises(self.gps.GpsInputError):
            self.gps.parse_stm32_telemetry(corrupted)

    def test_crc_matches_frontend_encoder(self) -> None:
        """양쪽 CRC 구현 동치성: base JSON 재인코딩 시 같은 CRC가 나와야 한다."""
        import re

        for line in self.lines:
            if not line.startswith("{"):
                continue
            match = re.fullmatch(r'(.*),"crc16":"([0-9A-F]{4})"}', line)
            self.assertIsNotNone(match, line)
            base = match.group(1) + "}"
            expected = int(match.group(2), 16)
            self.assertEqual(
                self.gps.crc16_ccitt(base.encode("ascii")), expected, line
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
