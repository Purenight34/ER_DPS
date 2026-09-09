# 이터널 리턴 기본 공격 실험 계산기

**유키와 장비 이름을 선택하고, 스탯·기본 공격 계산을 실제 게임 관측값과 비교하는 C++20 콘솔 프로그램**이다.
공식 API 스냅샷의 수치와 사용자가 승인한 `basic-attack-experiment-v1` 임시 모델을 사용한다.
데스크톱 UI는 아직 구현하지 않았다. 실행 중에는 Python, 네트워크, API 키 또는 AI 모델이 필요하지 않다.

## 유키로 바로 실행하기

프로젝트의 `run_yuki.cmd`를 더블클릭하거나, 프로젝트 루트의 PowerShell에서 실행한다.

```powershell
.\run_yuki.cmd
```

콘솔 메뉴의 시작 조건은 다음과 같다.

- 공격자: 유키, 캐릭터 레벨 1, 무기 숙련도 레벨 1
- 공격자 무기: 녹슨 검(양손검), 추가 방어구·특성 없음, 단추 0개
- 방어자: 더미, 최대·시작 체력 1,000, 방어력 0, 보호막 0
- 더미에는 장비·특성·숙련도·패시브·모드 보정이 없다.
- 공격: 일반 기본 공격 1회, 측정 구간 1초
- 최종 공격 속도·치명타 확률·치명타 배율: 미입력

Enter 또는 1번으로 계산한다. 2·3번에서 공격자·방어자 설정을 바꾸고 4번에서 타격·실측값을 입력한다.
3번에서는 더미의 체력·방어력을 바꾸거나 수비 대상을 실험체로 전환할 수 있다.
**9번은 현재 설정과 결과를
`outputs/yuki_test.ini`, `outputs/yuki_test.txt`, `outputs/yuki_test.json`에 저장**한다.
피해 계산을 지원하지 않는 조건에서는 텍스트에 차단 사유를 기록하고 JSON에는 `status: blocked`를 저장한다.
동일 경로의 이전 저장 파일은 덮어쓴다. 다시 실행하면 기본 조건으로 시작하며 이전 저장을 자동으로 읽지 않는다.
저장한 설정은 메뉴 8번 또는 아래 `--loadout` 명령으로 불러온다.

현재 스냅샷의 기본 공격자 공격력은 `36 + 11 = 47`이다.
무기 숙련도 증폭 `1 × 0.022`를 적용하면 방어 전 피해 `48.034`이며,
기본 더미는 방어력 0이므로 최종 피해도 `48.034`이다. 이는 승인된 임시 식의 결과이며 게임에서 관측한 정수 피해를 뜻하지 않는다.

게임과 비교할 때는 유키의 단추를 모두 소모한 상태를 유지하고 Q를 사용하지 않는다.
단추가 다시 충전되지 않도록 전투를 유지하며 다른 버프·패시브·특성·조건부 장비 효과가 개입하지 않게 조건을 기록한다.
관측값과 차이는 [code_fix.md](docs/code_fix.md)에 남긴다.

## 파일 실행과 이름 검색

실행 파일에 인자가 없거나 `--interactive`를 주면 콘솔 메뉴를 연다.
명령은 프로젝트 루트에서 실행한다.

```powershell
.\build\zig\er_calc.exe --interactive
.\build\zig\er_calc.exe --loadout .\examples\yuki.ini
.\build\zig\er_calc.exe --loadout .\examples\yuki.ini --format json --output .\results\yuki.json
.\build\zig\er_calc.exe --loadout .\outputs\yuki_test.ini
.\build\zig\er_calc.exe --list characters --search 유키
.\build\zig\er_calc.exe --list weapons --search "녹슨 검"
.\build\zig\er_calc.exe --list armor
.\build\zig\er_calc.exe --list traits
```

기본 카탈로그는 `data/catalog.tsv`이다. 다른 스냅샷에서 만든 카탈로그는
`--catalog 다른경로/catalog.tsv`로 지정한다. 이름 선택은 정확한 한국어 이름 또는 공식 코드로
처리하며 검색은 이름·코드의 부분 문자열을 사용한다. 이름이 중복되면 공식 코드로 구분한다.
이전 저장 파일에 수비 실험체 이름이 명시돼 있으면 그 조건을 유지한다. 새 기본 더미로 바꾸려면
메뉴 7번으로 기본값을 초기화하거나 3번에서 더미를 선택한다.

캐릭터·장비·특성의 목록을 조회할 수 있지만 피해 계산 범위는 **양손검 유키, 단추 0,
특성 없음, 지원되는 고정 스탯 장비**로 제한된다. 다른 실험체, 쌍검, 단추 추가 피해,
특성 및 고유·조건부 효과가 확인되지 않은 영웅 등급 이상 장비는 스탯·진단을 표시하고 피해 비교를 차단한다.
API의 이름·설명을 볼 수 있다는 이유로 효과 계수·발동 조건까지 계산 가능한 것으로 취급하지 않는다.

한 번의 일반 기본 공격은 최종 공격 속도와 치명타 수치를 모르는 상태에서도 비교할 수 있다.
여러 기본 공격에는 최종 공격 속도가 필요하고, 치명타 적중에는 최종 치명타 배율이 필요하다.
치명타 발생 여부는 타격별 입력을 사용하며 난수를 생성하거나 확률 가중 기대 피해를 출력하지 않는다.

## 빌드와 오프라인 테스트

`run_yuki.cmd`에서 실행 파일이 없다는 안내가 나오면 먼저 빌드한다. 실행 스크립트는 자동 빌드하지 않는다.
현재 작업 환경의 테스트용 Zig 0.16.0은 `.tools/zig-x86_64-windows-0.16.0/`에 있다.
이는 컴파일러로만 사용한다. `.tools`와 `build`는 Git에 포함하지 않는다.
다른 환경에서는 C++20 컴파일러를 준비해야 하며 빌드 스크립트가 다운로드를 수행하지는 않는다.

```powershell
.\scripts\build.ps1
```

다른 위치의 Zig는 `scripts/build.ps1 -ZigPath '절대경로/zig.exe'`로 지정한다.
스크립트는 코어·입출력·카탈로그·이름 기반 설정·콘솔 메뉴 테스트와 CLI JSON 검증을 실행한다.
컴파일 로그와 캐시는 `build/zig` 아래에 저장한다. 기본 테스트는 fixture를 사용하고 live API를 호출하지 않는다.

CMake 3.20 이상과 C++20 컴파일러를 갖춘 환경에서도 빌드할 수 있다.

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake --config Release
ctest --test-dir build/cmake -C Release --output-on-failure
```

Python 데이터 도구의 오프라인 테스트는 `requirements.txt`의 의존성이 설치된 Python에서 실행한다.

```powershell
python -m unittest discover -s tests -p "test_*.py"
```

## 직접 수치를 입력하는 실험

기존 수치 입력도 유지한다. `examples/basic_attack.ini`는 합성 fixture이고,
`examples/game_experiment.ini`는 실제 실험 조건을 직접 기록하는 양식이다.
이 형식은 이름을 적는 것만으로 장비나 특성 효과를 적용하지 않는다.
지원하지 않는 효과가 개입한 입력은 `unsupported_effects=true`로 선언해야 하며 계산기는 오류를 반환한다.

```powershell
.\build\zig\er_calc.exe .\examples\basic_attack.ini
.\build\zig\er_calc.exe .\examples\basic_attack.ini --format json --output .\results\sample.json
```

기본 스탯·레벨 성장·출처별 고정 추가값, 무기 숙련도와 레벨별 증폭률, 초기 체력·보호막,
패치·데이터·공식 버전, 타격 시각과 관측값을 입력한다. 소수점은 `.`이고 확률·증폭률은
0~1 비율이다. 치명타 확률 25%는 `0.25`, 숙련도 레벨당 증폭 2%는 `0.02`이다.
치명타 배율은 최종 배율이므로 175%는 `1.75`이며 현재 게임 기본값을 뜻하지 않는다.

```ini
# ID, 측정 시작 기준 시간(ms), hit/miss, normal/critical, 관측 총 피해 또는 na
attack=aa-1,0,hit,normal,60
attack=aa-2,1000,hit,critical,na
attacker.attack_power.flat=weapon-official-id,10
```

모든 타격은 `[0, duration_ms)` 안에 있어야 한다. `na`는 미측정이며 실측 피해 0과 구분한다.
실측 비교는 보호막·체력 피해의 합을 기준으로 `계산값 - 실측값`을 출력하고,
일부만 측정했다면 측정된 타격끼리만 합산한다. 내부 소수 결과를 게임 표시값에 맞춰 임의로 반올림하지 않는다.
샘플 `basic_attack.ini`의 총 피해는 225, 4초 구간 콤보 DPS는 56.25이다.

## 데이터와 책임 경계

현재 데이터 버전은 `api-3acdbbe75d1ba8bf6e0c4c1f`이다. 공식 API가 게임 패치 번호를
제공하지 않아 결과에는 `API 미제공`으로 표시하며 조사한 패치 번호를 임의로 대입하지 않는다.
이미 저장된 카탈로그로 실험할 수 있고 데이터 업데이트는 별도 명령으로만 수행한다.

- `combat/`: C++20 영구 스탯·피해·집계·검증. 파일과 네트워크를 읽지 않는다.
- `catalog/`: 공식 이름·코드 조회와 이름 기반 설정을 계산 입력으로 변환한다.
- `cli/`: 콘솔 메뉴·입력·결과 출력. 피해식을 중복 구현하지 않는다.
- `data/`: 버전별 원본 스냅샷과 정규화된 카탈로그.
- `update_version.py`, `normalize_catalog.py`: Python 데이터 동기화·정규화 보조 도구.
- [formulas.md](docs/formulas.md): 공식 근거, 승인된 임시 식과 미확정 규칙.
- [data.md](docs/data.md): 데이터 모델·버전·동기화 및 정규화 명령.
- [code_fix.md](docs/code_fix.md): 실측·가설·승인·추가 검증 기록 양식.

기존 `api_connection_test.py`의 유저 조회 경로는 게임 데이터 전용 네트워크 규칙에 따라 실행하지 않는다.
