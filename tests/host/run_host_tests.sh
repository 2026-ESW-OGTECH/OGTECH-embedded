#!/bin/sh
# 호스트(gcc) 테스트 실행기 — 하드웨어 없이 프로토콜·명령·통합 경로를 검증한다.
# 사용법: tests/host/run_host_tests.sh
set -e
cd "$(dirname "$0")"

CC="${CC:-gcc}"
SRC=../../Core/Src
INC=../../Core/Inc
mkdir -p build

# 순수 프로토콜 모듈 — 펌웨어에 넣는 코드 그대로이므로 -Werror를 유지한다.
"$CC" -std=c99 -Wall -Wextra -Werror -pedantic -I"$INC" \
  -o build/test_protocol test_protocol.c "$SRC"/telemetry_protocol.c

# 펌웨어 전체 시뮬레이션 — mock main.h(-I.)로 HAL을 대체해 Core/Src 전부를 컴파일한다.
"$CC" -std=c99 -Wall -Wextra -Werror -I. -I"$INC" \
  -o build/test_firmware_sim test_firmware_sim.c "$SRC"/telemetry_protocol.c

./build/test_protocol
./build/test_firmware_sim
echo "host C tests OK"
