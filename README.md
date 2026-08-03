# SafeAid Kit Embedded

STM32 상시 전원 관리자·센서 허브·GNSS 로거를 위한 펌웨어 저장소입니다.

## 구현 상태

이 저장소는 오지 생존 도메인 전환 중입니다. 현재 `.ino` 파일은 목표 P0 설계와 완전히 일치하지 않을 수 있습니다.
새 STM32 상시 계층의 구현 범위와 완료 조건은 [조직 PLAN](https://github.com/SmartAid-Kit/.github/blob/main/PLAN.md) 및 저장소 이슈에서 관리합니다.

## 목표 역할

- GNSS 자동 위치 로깅과 측위 상태 전달
- CO·환경·전원 센서 허브
- Jetson 전원 게이팅
- Jetson 없이 동작하는 CO 경보·부저·진동·스트로브
- 물리 버튼·홀 센서·저온 충전 차단

GPS 미수신 상태는 확정 좌표로 바꾸지 않습니다. 센서가 확인하지 않은 상태를 정상처럼 표시하지 않습니다.

## 범위 밖 코드

기존 ESP32 캡처 코드는 이력 보존을 위해 남아 있지만, 카메라 기능은 현 도메인 범위 밖이며 현재 사용하지 않습니다.

## 검증

Arduino CLI 또는 PlatformIO 도구체인이 준비된 환경에서 빌드 결과를 기록합니다. 도구체인이 없으면 빌드 통과로 간주하지 않습니다.

상세 안전 규칙은 [AGENTS.md](https://github.com/SmartAid-Kit/.github/blob/main/AGENTS.md)를 따릅니다.
