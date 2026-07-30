# SafeAid Kit AI 에이전트 가이드

이 문서는 SafeAid Kit에서 작업하는 Codex, Claude Code, 그리고 향후 모든 코딩 에이전트를 위한 기준 가이드입니다.

## 미션

SafeAid Kit는 2026 임베디드 소프트웨어 경진대회 자유공모 부문 출품작입니다. 프로젝트 목표는 심사위원이 실제 하드웨어의 안정적인 동작을 보고 이해할 수 있는 오프라인 재난 및 캠핑용 스마트 구급함을 만드는 것입니다.

목표 결과:

- 최소 목표: 결선 진출.
- 최대 목표: 대상 수상.
- 전략: 소프트웨어는 기본적으로 안정적이어야 하며, 가장 큰 효과를 내는 작업은 물리 하드웨어 품질, 데모 신뢰성, 문서 명확성, 제출 준비도입니다.

## 대회 사실

- 공식 제출 마감일: 2026-09-03.
- 내부 동결 마감일: 2026-09-01 18:00 KST.
- 예선 필수 제출물: 20쪽 이내 개발 보고서 PDF, GitHub URL, 3분 이내 YouTube 시연 영상.
- 자유공모 심사 배점: 창의성 30, 기술성 및 완성도 30, 활용성 20, 의사소통 능력 10, 팀 구성 및 개발 역량 10.
- 제출 전 다시 확인해야 할 공식 출처:
  - https://www.eswcontest.or.kr/competition/free.php
  - https://www.eswcontest.or.kr/competition/download.php?type=f3
  - https://www.eswcontest.or.kr/community/notice.php?code=notice&idx=4352&page=2&ptype=view

## 핵심 규칙

- LED 서랍 안내, 센서, 카메라 노드, Pi 터치 UI, 전원 신뢰성, 외함, 배선, 데모 스크립트처럼 하드웨어와 맞닿은 변경을 우선하세요.
- 파일 및 문서 생성/수정 시 기본 작성 언어는 한국어입니다. 코드 식별자, 명령어, API 이름, 외부 제출 양식, 직접 인용처럼 원문 유지가 필요한 경우만 예외로 합니다.
- 의학적 진단이나 치료 주장을 추가하지 마세요.
- LLM이 응급처치 지침을 생성하게 하지 마세요. 로컬 LLM 출력은 구조화된 분류로 제한됩니다.
- `safeaid_core.py`의 고정 응급처치 카드 내용을 사용자에게 표시되는 절차 문구의 기준 소스로 유지하세요.
- 오프라인 동작을 항상 보존하세요. 네트워크 의존 기능은 선택 사항이어야 하며 로컬 대체 경로가 있어야 합니다.
- 공개 GitHub를 최종 공개 대상이라고 보고 작업하세요. 비밀값, Wi-Fi 자격 증명, 개인정보, 비공개 Notion 내보내기, 공개 전 심사 자료를 커밋하지 마세요.
- Notion 작업은 반드시 `my_workspace`만 사용하세요. SafeAid Kit에서는 절대 `notion_guest`를 사용하지 마세요.
- 공식 대회 페이지가 변경되면 `docs/SUBMISSION_CHECKLIST.md`와 `docs/ROADMAP.md`를 업데이트하세요.

## 로컬 LLM 계약

로컬 LLM 분류기는 아래 형태만 반환할 수 있습니다.

```json
{
  "scenario_id": "bleeding",
  "confidence": "low|medium|high",
  "risk_flags": ["massive_bleeding"]
}
```

검증 규칙:

- `scenario_id`는 `SCENARIOS`의 키 중 하나여야 합니다.
- 알 수 없거나 유효하지 않은 출력은 키워드 분류기로 대체합니다.
- `risk_flags`는 결정론적 위험 감지 결과와 병합해야 합니다.
- LLM 출력에서 생성된 지침, 안심시키는 문구, 진단, 약물 조언, 용량, 응급 출동 판단은 화면에 표시하면 안 됩니다.

## 저장소 작업 흐름

- 작고 검토하기 쉬운 변경을 선호하세요.
- 프로젝트 운영과 대회 전략은 `docs/` 아래에 두세요.
- 코드 테스트는 `tests/` 아래에 두세요.
- 펌웨어는 `firmware/` 아래에 두세요.
- 런타임 출력은 `runtime/` 아래에 두고 Git에서 무시하세요.
- GitHub 저장소가 생기면 풀 리퀘스트를 사용하세요.
- 이슈 라벨은 `hardware`, `firmware`, `local-llm`, `ui`, `docs`, `submission`, `test`, `safety`를 사용하세요.
- 코드 변경을 마치기 전 필수 확인:

```powershell
python -m unittest discover -v
```

필요하면 더 좁은 테스트를 먼저 실행할 수 있지만, 기준 명령은 위의 discover 명령입니다.

## 완료의 정의

변경은 아래 조건을 만족할 때만 완료된 것입니다.

- 대회와 관련된 결과를 개선합니다.
- 사용자에게 보이는 동작 또는 하드웨어와 맞닿은 동작이 명확합니다.
- 테스트 또는 수동 하드웨어 검증 메모가 제공됩니다.
- 설정, 배선, 작동 방식, 제출 동작이 바뀌었다면 README 또는 문서를 업데이트했습니다.
- Notion 후속 작업은 `my_workspace`를 참조합니다.
