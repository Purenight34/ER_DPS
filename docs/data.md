# 게임 데이터·이름 기반 입력 모델

## 현재 버전과 파일

2026-09-09 저장한 공식 스냅샷의 데이터 버전은 `api-3acdbbe75d1ba8bf6e0c4c1f`이다.
패치 번호는 공식 API가 제공하지 않으므로 원본 manifest에는 `not-provided-by-api`,
정규화 카탈로그와 결과에는 `API 미제공`으로 남긴다. 별도로 조사한 패치 번호와 자동 연결하지 않는다.

- `data/current.json`: 현재 스냅샷 상대 경로, 데이터 버전, manifest의 SHA-256.
- `data/snapshots/<data_version>/manifest.json`: 조회 시각, 공식 문서 출처, 테이블별 공식 hash와 파일 SHA-256, 한국어 파일 SHA-256.
- 같은 스냅샷의 `<Table>.json`, `Korean.txt`: 인증 정보를 포함하지 않는 공식 원본 행과 한국어 문자열.
- `data/catalog.tsv`: C++ 실행 파일이 읽는 UTF-8 이름·필드 카탈로그.

원본 스냅샷은 로컬 캐시로 보관하고 Git에서는 제외한다. 카탈로그는 저장소에 포함해
다른 환경에서도 API 키 없이 실행할 수 있게 한다. 현재 카탈로그에는 10개 테이블의 2,004개 행이 있다.

현재 원본 스냅샷에는 다음 12개 테이블이 있다.

| 용도 | 테이블 |
| --- | --- |
| 실험체와 성장 | `Character`, `CharacterLevelUpStat` |
| 무기 착용·종류·숙련도 | `CharacterMastery`, `WeaponTypeInfo`, `MasteryLevel`, `MasteryStat` |
| 장비 | `ItemWeapon`, `ItemArmor` |
| 모드·특성·보조 정보 | `CharacterModeModifier`, `Trait`, `CharacterAttributes`, `Level` |

카탈로그는 계산·선택에 필요한 테이블과 `Trait`를 정규화한다. 원본에 없는 테이블이나
효과 파라미터를 생성하지 않는다. `CharacterAttributes`, `Level` 등 보관된 모든 테이블이
곧 계산에 사용된다는 뜻은 아니다. 특성 설명의 자리표시자와 장비의 고유 효과 정보만으로는
발동 조건·계수·순서를 채울 수 없으므로 현재 특성과 고유 효과가 미확인인 장비의 피해 비교는 제한한다.

## 데이터 업데이트 명령

Python은 데이터 동기화와 형식 변환에만 사용한다. 계산 실행 파일은 Python에 의존하지 않는다.
아래 명령은 `requirements.txt`의 `requests`, `python-dotenv`가 설치된 Python 환경에서
프로젝트 루트를 기준으로 실행한다. 저장된 카탈로그로 계산할 때는 이 절차가 필요하지 않다.

```powershell
python update_version.py --help
python update_version.py --data-dir data
```

동기화는 필수 6개 테이블과 공식 hash 목록에 존재하는 선택 테이블을 조회한다.
추가 테이블은 `--tables Trait`처럼 지정하며 공식 hash 목록에 없는 이름은 거부한다.
`--raw-only`는 현재 기본 동작을 명시하는 옵션이다. 이 명령은 원본 스냅샷을 저장하며
카탈로그 생성은 다음 명령으로 별도 실행한다.

```powershell
python normalize_catalog.py --snapshot data/snapshots/api-3acdbbe75d1ba8bf6e0c4c1f --output data/catalog.tsv
```

업데이트 뒤에는 `data/current.json`이 가리키는 스냅샷 경로를 `--snapshot`에 사용한다.
`--patch-version`은 확인된 패치 번호가 있을 때만 명시한다. 기본값 `API 미제공`을
자료 없이 바꾸지 않는다. 카탈로그 파일을 바꾼 후 프로그램을 다시 실행하면 그 버전으로 계산한다.

공식 API 접근은 `settings.py`의 `ER_API_KEY` 접근 함수 경계를 통한다.
`GET /v2/data/hash`, `GET /v2/data/{metaType}`, `GET /v1/l10n/Korean`과
공식 한국어 메타데이터가 제공하는 공개 HTTPS 파일 다운로드만 사용한다.
공개 파일 다운로드에는 API 인증 헤더를 전달하지 않는다.
요청 시작 간격은 최소 1.2초, timeout은 15초이며 429와 지정된 일시적 5xx에만
최대 3회 재시도한다. 인증 헤더·응답 본문·키 값을 로그로 출력하지 않는다.

변경되지 않은 테이블은 공식 hash와 저장 파일의 SHA-256을 확인해 재사용한다.
동기화 전후 공식 hash가 바뀌면 새 스냅샷을 게시하지 않는다. 네트워크·구조 검증을 마친 뒤
파일을 저장하고 `current.json`을 갱신한다. 동일한 스냅샷은 기존 조회 시각을 유지한다.
기본 테스트는 live API를 호출하지 않으며 키가 없을 때는 fixture 기반 오프라인 검증만 진행한다.

## 카탈로그 형식과 책임

카탈로그 첫 줄은 `# er-catalog-v1`이고 각 데이터 줄은 다음 네 필드를 탭으로 구분한다.

```text
table<TAB>row_id<TAB>field<TAB>value
```

`row_id`는 원본 테이블 내 행을 묶는 식별자이고, 공식 고유 코드는 별도 `code` 필드에
보존한다. 역슬래시·탭·개행·캐리지 리턴은 `\\`·`\t`·`\n`·`\r`로 이스케이프한다.
숫자·배열·불리언은 원본 JSON 값을 문자열로 보존한다. 정규화 단계는 스탯 계산,
단위 변환 또는 효과 계수 보충을 수행하지 않는다.

`_name`과 `_description`은 공식 한국어 문자열을 연결한 표시 필드이다.
번역이 없으면 원본 이름 또는 코드에 `[번역 없음]`을 붙이며 임의 번역하지 않는다.
`Meta`에는 데이터 버전·패치 표시·출처·원본 manifest를 기록한다.
정규화 전에 각 원본 JSON과 한국어 파일의 SHA-256을 manifest와 비교하며 불일치하면
기존 카탈로그를 유지하고 오류를 반환한다. 한국어 장비 설명은 `Item/Help/<code>`,
특성 설명은 `Trait/Tooltip/<code>`로 연결한다. 장비 설명은 고유 효과 계수가 아니라 설명·배경 문구일 수 있다.

C++의 `CatalogRecord`는 테이블·행 ID·필드 모음이고 `Catalog`는 이름·공식 코드 조회 및
부분 문자열 검색을 제공한다. 누락되거나 중복된 정확한 이름은 오류로 처리한다.
사용자는 중복 이름을 공식 코드로 구분할 수 있다.

## 이름 기반 실험에서 계산 입력으로

`NamedLoadout`은 실험체·무기·방어구·특성 이름 또는 공식 코드, 캐릭터 레벨,
무기 숙련도 레벨과 유키 단추 수를 보관한다. `NamedExperiment`는 공격자·방어자 설정,
측정 구간·타격 목록·선택적인 최종 공격 속도·치명타 수치·초기 체력·보호막을 담는다.

수비 대상은 `DefenderKind::Dummy`(기본) 또는 `Character`로 구분한다. 기본 더미는
사용자 지정 최대 체력 1,000·방어력 0을 가지며 캐릭터·장비·숙련도·패시브·모드 테이블을
수비 계산에 참조하지 않는다. 내부 코어 입력은 공격력 0, 성장값·장비 추가값 0으로 만들고
`더미 [test-dummy]`와 `none` 메타데이터로 구분한다. 내부 `level=1`은 성장 없는 고정 입력을
위한 형식 값이며 더미에게 캐릭터 레벨이나 숙련도가 있다는 의미가 아니다.

이름 기반 입력의 기본 수비 설정은 다음과 같다.

```ini
[defender]
type=dummy
max_hp=1000
defense=0
```

`type=character`이면 기존 실험체·장비·숙련도 필드를 사용한다. 이전 파일처럼 `type` 없이
수비 실험체 관련 필드가 있으면 `character`로 읽어 저장된 조건을 유지한다. 더미 수치와
실험체 필드를 혼합하면 오류를 반환한다. 수비 설정 자체가 생략되면 기본 더미를 사용한다.
콘솔 3번에서 유형과 더미 수치를 변경한다. 유형 변경 시 시작 체력은 새 대상의 최대 체력,
보호막은 0으로 초기화한다. 더미의 최대 체력을 변경해도 시작 체력을 그 값으로 초기화한다.
설정 저장·불러오기에는 수비 유형과 더미 수치가 포함된다.

`resolve_experiment`는 공식 행을 확인하고 지원되는 기본값·성장값·고정 장비 수치·무기
숙련도 증폭을 `Scenario`로 연결한다. 결과인 `ResolvedExperiment`는 계산 입력과 함께
확인할 안내(`notices`), 계산 차단 사유(`blockers`)를 가진다. 피해 계산과 집계는
`combat/`의 기존 엔진이 수행한다. 표시 이름 문자열로 피해 공식을 구현하지 않는다.

카탈로그 수치가 있어도 다음 조건은 자동으로 계산하지 않는다.

- 다른 실험체의 기본 공격·패시브, 유키 쌍검 또는 단추 추가 피해.
- 특성의 효과, 미확인 고유·조건부 장비 효과, 관통·피해 증폭·감소 및 0이 아닌 모드 보정.
- 미지원 숙련도 효과 또는 레벨 구간마다 달라지는 증폭률의 적용 경계.
- 최종 공격 속도와 치명타 확률·배율의 합산식.

한 번의 일반 공격은 마지막 항목을 미입력 상태로 비교할 수 있다. 다회 공격에는 최종 공격 속도,
치명타 적중에는 최종 배율이 필요하다. 미입력은 `*_known=false`로 전달·출력하고 실제 0과 구분한다.
콘솔에서는 실험 조건을 간략히 표시하고 JSON의 `scenario.notes`에는 원본 수치의 미적용 내역도 보존한다.

`--loadout examples/yuki.ini`는 이름 기반 입력을 읽는다. 인자 없는 실행 또는 `--interactive`는
기본 유키 설정으로 시작한다. 메뉴 9번은 `outputs/yuki_test.ini`와 `outputs/yuki_test.txt`,
`outputs/yuki_test.json`을 저장한다. 미지원 조건의 JSON은 피해 결과 대신 `status: blocked`를
기록해 이전 정상 결과가 남지 않게 한다. 저장된 입력은 자동 로드하지 않으며
다시 사용하려면 메뉴 8번 또는 `--loadout outputs/yuki_test.ini`를 명시한다.
