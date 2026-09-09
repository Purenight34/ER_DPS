# 이터널 리턴 기본 공격 실험 계산기

UI에 앞서 **C++20 콘솔에서 스탯·기본 공격 계산과 실측 비교**를 수행한다.
현재 규칙은 사용자가 승인한 `basic-attack-experiment-v1` 임시 모델이다.
공식 게임 계산 전체를 검증한 제품이 아니며, 실제 게임 데이터 캐시는 아직 없다.

현재 지원 범위:

- 공격자·방어자의 공격력, 방어력, 최대 체력: 기본값·레벨 성장·출처별 고정 추가값 계산
- 무기 숙련도 레벨별 기본 공격 증폭
- 각 타격에 명시한 일반/치명타 및 Hit/Miss 조건
- 방어 적용 전후 피해, 보호막·체력 피해, 사망 이후 초과 피해
- 정수 밀리초 타임라인과 입력한 최종 공격 속도에 따른 최소 간격 검증
- 총 콤보 피해, 명시한 구간의 콤보 DPS, 타격별 실측 오차
- 전체 입력·버전·계산 내역의 콘솔 또는 JSON 파일 출력

계산 경로에 Python, HTTP, API 키, AI 모델이 필요하지 않다.
장비/특성 선택 UI, API 데이터 자동 연결, 스킬·버프·패시브·관통·모드 보정은 아직 지원하지 않는다.
관련 조건이 있으면 입력의 `unsupported_effects=true`로 표시하고, 계산기는 오류를 반환한다.
장비·특성 등의 메타데이터 문자열만 적어도 효과가 자동 적용되는 것은 아니다.

## Windows에서 실행

현재 작업 환경에는 테스트용 Zig 0.16.0이 `.tools/zig-x86_64-windows-0.16.0/`에 준비되어 있다.
이는 C++ 컴파일러로만 사용하며 결과 실행 파일은 Zig나 Python 없이 실행한다.
`.tools`와 `build`는 Git에 포함하지 않는다. 다른 환경에서는 [공식 Zig 다운로드](https://ziglang.org/download/)
또는 C++20 컴파일러를 별도로 준비한다. 빌드 스크립트는 네트워크 다운로드를 수행하지 않는다.

프로젝트 루트의 PowerShell에서:

```powershell
.\scripts\build.ps1
.\build\zig\er_calc.exe .\examples\basic_attack.ini
.\build\zig\er_calc.exe .\examples\basic_attack.ini --format json --output .\build\zig\sample-result.json
```

다른 위치의 Zig를 사용할 때는 `scripts/build.ps1 -ZigPath '절대경로/zig.exe'`를 사용한다.
스크립트는 C++20으로 빌드하고 코어·입출력 테스트 및 CLI JSON 검증을 실행한다.
모든 기본 테스트는 합성 fixture를 사용하고 live API를 호출하지 않는다.

CMake 3.20 이상과 C++20 컴파일러를 갖춘 환경에서는 다음 경로도 제공한다:

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake --config Release
ctest --test-dir build/cmake -C Release --output-on-failure
```

## 게임 실험과 비교하기

1. `examples/game_experiment.ini`를 별도 파일로 복사하고 `PLACEHOLDER` 안내값을 실제 조건으로 채운다.
2. 캐릭터·무기·장비·특성·스킬 레벨·숙련도·초기 상태·모드·패치 및 데이터 출처를 기록한다.
3. 기본 스탯, 성장값, 출처별 고정 추가값, 최종 공격 속도, 최종 치명타 확률/배율,
   무기 숙련도 레벨과 레벨별 기본 공격 증폭률을 입력한다. 알려지지 않은 값은 추측하지 않는다.
4. 측정 구간 길이와 실제 타격 시간을 밀리초로 입력한다. 첫 타격은 0이어도 되며,
   모든 타격은 `[0, duration_ms)` 안에 있어야 한다. 마지막 타격 이후까지 측정 구간을 명시한다.
5. 각 공격의 Hit/Miss와 일반/치명타 여부, 관측 피해를 입력한 뒤 계산기를 실행한다.
6. 출력과 관측 기록을 [code_fix.md](docs/code_fix.md)에 함께 남긴다.

숫자 입력은 소수점 `.`을 사용한다. 확률·증폭률은 **0~1 비율**로 입력하므로
치명타 확률 25%는 `0.25`, 숙련도 레벨당 증폭 2%는 `0.02`이다.
`critical_multiplier`는 **최종 배율**이며 175%이면 `1.75`이다. 현재 게임 기본값을 의미하지 않는다.
`amplification_level_source=weapon_mastery`, `amplification_levels=무기 숙련도 레벨`로 입력한다.
캐릭터의 `level`은 스탯 성장에만 사용한다.

반복 공격 행 형식:

```ini
# ID, 측정 시작 기준 시간(ms), hit/miss, normal/critical, 관측 총 피해 또는 na
attack=aa-1,0,hit,normal,60
attack=aa-2,1000,hit,critical,na
# 같은 스탯에 여러 출처를 더할 수 있다. 같은 source_id의 중복은 오류이다.
attacker.attack_power.flat=weapon-official-id,10
```

`na`는 미측정이다. 실측 피해 0과 다르게 처리한다.
비교 대상 실측값은 보호막과 체력에 가한 피해를 합한 값이다.
출력의 오차는 `계산값 - 실측값`, 오차율은 `오차 / 실측값 × 100`이다.
일부만 측정했다면 측정된 타격의 계산값과 실측값만 합산하여 비교한다.
게임에 표시된 정수 피해와 코어의 소수 결과 차이를 임의 반올림으로 숨기지 않는다.

샘플 `basic_attack.ini`는 실제 게임값이 아닌 합성 데이터이다.
공격력 100, 숙련도 10×2%, 방어력 100에서 일반 적중 60, 치명타 적중 105,
빗나감 0, 일반 적중 60을 계산한다. 4초 구간 총 피해는 225, 콤보 DPS는 56.25이다.

## 책임 경계와 문서

- `combat/`: C++20 영구 스탯·피해·집계·검증. 파일과 네트워크를 읽지 않는다.
- `cli/`: 입력 검증·결과 출력. 수식을 중복 구현하지 않는다.
- `tests/`: 합성 수치 기반 오프라인 회귀 테스트.
- `examples/`: 실행 가능한 합성 예시와 실제 실험용 입력 양식.
- [formulas.md](docs/formulas.md): 공식 근거, 승인된 임시 식, 지원 범위와 미확정 규칙.
- [code_fix.md](docs/code_fix.md): 실제 실험·가설·승인·추가 검증 기록 양식.

기존 Python API 조회 파일은 이번 C++ 계산 경로 및 기본 테스트에서 사용하지 않는다.
API 동기화 및 버전 캐시는 별도 후속 작업이다. 기존 `api_connection_test.py`는 유저 조회 코드이므로
이 프로젝트의 게임 데이터 전용 네트워크 규칙에 따라 실행하지 않는다.
