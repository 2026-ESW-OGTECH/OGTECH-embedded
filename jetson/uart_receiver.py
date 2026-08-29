"""Receive and validate OGTECH STM32 telemetry on a Jetson UART."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass


CO_STATES = {0: "WARMING_UP", 1: "VALID", 2: "STALE"}
GPS_STATES = {0: "NOT_FOUND", 1: "NO_FIX", 2: "FIX"}


class FrameError(ValueError):
    """Raised when a telemetry frame is malformed or fails checksum."""


@dataclass(frozen=True)
class Telemetry:
    sequence: int
    uptime_ms: int
    dht_valid: bool
    temperature_c: float | None
    humidity_percent: float | None
    co_state: str
    co_ppm: int | None
    gps_state: str
    latitude: float | None
    longitude: float | None
    satellites: int


def xor_checksum(payload: str) -> int:
    checksum = 0
    for byte in payload.encode("ascii"):
        checksum ^= byte
    return checksum


def parse_frame(raw: str | bytes) -> Telemetry:
    if isinstance(raw, bytes):
        try:
            line = raw.decode("ascii").strip()
        except UnicodeDecodeError as exc:
            raise FrameError("frame is not ASCII") from exc
    else:
        line = raw.strip()

    if not line.startswith("$") or "*" not in line:
        raise FrameError("missing frame marker")

    body, checksum_text = line[1:].rsplit("*", 1)
    if len(checksum_text) != 2:
        raise FrameError("checksum must contain two hex digits")

    try:
        expected = int(checksum_text, 16)
    except ValueError as exc:
        raise FrameError("checksum is not hexadecimal") from exc

    actual = xor_checksum(body)
    if actual != expected:
        raise FrameError(f"checksum mismatch: expected {expected:02X}, got {actual:02X}")

    fields = body.split(",")
    if len(fields) != 12 or fields[0] != "OGT1":
        raise FrameError("unsupported protocol or field count")

    try:
        sequence = int(fields[1])
        uptime_ms = int(fields[2])
        dht_valid = bool(int(fields[3]))
        temperature_x10 = int(fields[4])
        humidity_x10 = int(fields[5])
        co_state_code = int(fields[6])
        co_ppm = int(fields[7])
        gps_state_code = int(fields[8])
        latitude_e7 = int(fields[9])
        longitude_e7 = int(fields[10])
        satellites = int(fields[11])
    except ValueError as exc:
        raise FrameError("a numeric field is invalid") from exc

    if co_state_code not in CO_STATES:
        raise FrameError(f"unknown CO state {co_state_code}")
    if gps_state_code not in GPS_STATES:
        raise FrameError(f"unknown GPS state {gps_state_code}")
    if not 0 <= satellites <= 255:
        raise FrameError("satellite count is outside uint8 range")

    gps_valid = gps_state_code == 2
    return Telemetry(
        sequence=sequence,
        uptime_ms=uptime_ms,
        dht_valid=dht_valid,
        temperature_c=temperature_x10 / 10.0 if dht_valid else None,
        humidity_percent=humidity_x10 / 10.0 if dht_valid else None,
        co_state=CO_STATES[co_state_code],
        co_ppm=co_ppm if co_state_code == 1 else None,
        gps_state=GPS_STATES[gps_state_code],
        latitude=latitude_e7 / 10_000_000.0 if gps_valid else None,
        longitude=longitude_e7 / 10_000_000.0 if gps_valid else None,
        satellites=satellites,
    )


def self_test() -> None:
    body = "OGT1,7,12345,1,234,567,1,12,2,375465126,1270757141,9"
    frame = f"${body}*{xor_checksum(body):02X}\r\n"
    telemetry = parse_frame(frame)
    assert telemetry.sequence == 7
    assert telemetry.temperature_c == 23.4
    assert telemetry.humidity_percent == 56.7
    assert telemetry.co_ppm == 12
    assert telemetry.latitude == 37.5465126
    assert telemetry.longitude == 127.0757141
    print(frame.strip())
    print(json.dumps(asdict(telemetry), ensure_ascii=False))


def receive(port: str, baud: int) -> None:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

    with serial.Serial(port, baudrate=baud, timeout=1.0) as uart:
        while True:
            raw = uart.readline()
            if not raw:
                continue
            try:
                telemetry = parse_frame(raw)
            except FrameError as exc:
                print(f"DROP {exc}: {raw!r}", file=sys.stderr)
                continue
            print(json.dumps(asdict(telemetry), ensure_ascii=False), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyTHS1")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
    else:
        receive(args.port, args.baud)


if __name__ == "__main__":
    main()
