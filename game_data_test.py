from __future__ import annotations

import json
import sys
from typing import Any

from game_data_api import (
    EternalReturnApiError,
    get_available_meta_types,
    get_game_data,
)


# 각 테이블에서 앞에서부터 몇 행까지 출력할지 설정
ROW_LIMIT = 2


def print_table_preview(
    meta_type: str,
    row_limit: int = ROW_LIMIT,
) -> None:
    """
    지정한 게임 데이터 테이블을 호출하고
    행 개수와 앞부분을 출력한다.
    """

    print()
    print("=" * 70)
    print(f"{meta_type} 데이터 요청")
    print("=" * 70)

    data = get_game_data(meta_type)

    # 일반 게임 데이터 테이블은 보통 list[dict] 형식
    if isinstance(data, list):
        print(f"전체 행 개수: {len(data)}")

        if not data:
            print("데이터가 비어 있습니다.")
            return

        first_row = data[0]

        if isinstance(first_row, dict):
            print(f"첫 번째 행의 필드: {list(first_row.keys())}")

        print()
        print(f"앞에서부터 {min(row_limit, len(data))}개 행을 출력합니다.")

        for index, row in enumerate(
            data[:row_limit],
            start=1,
        ):
            print()
            print(f"----- Row {index} -----")
            print(
                json.dumps(
                    row,
                    ensure_ascii=False,
                    indent=2,
                )
            )

        return

    # hash처럼 dict 형식으로 반환되는 경우
    if isinstance(data, dict):
        print(f"전체 항목 개수: {len(data)}")

        for index, (key, value) in enumerate(
            data.items()
        ):
            if index >= row_limit:
                break

            print(f"{key}: {value}")

        return

    print(f"예상하지 못한 자료형입니다: {type(data)}")
    print(data)


def find_meta_types(
    meta_types: dict[str, Any],
    keyword: str,
) -> list[str]:
    """
    metaType 이름에서 특정 문자열을 포함한 테이블을 찾는다.

    예시:
        keyword="Trait"
        → Trait 관련 테이블 이름 검색
    """

    keyword_lower = keyword.lower()

    return sorted(
        table_name
        for table_name in meta_types
        if keyword_lower in table_name.lower()
    )


def main() -> int:
    try:
        # 1. 현재 서버에서 사용할 수 있는 모든 테이블 이름 확인
        meta_types = get_available_meta_types()

        print()
        print("=" * 70)
        print("현재 사용 가능한 데이터 테이블")
        print("=" * 70)
        print(f"전체 테이블 개수: {len(meta_types)}")

        # 관심 있는 테이블 이름 검색
        character_tables = find_meta_types(
            meta_types,
            "Character",
        )

        item_tables = find_meta_types(
            meta_types,
            "Item",
        )

        trait_tables = find_meta_types(
            meta_types,
            "Trait",
        )

        print()
        print("[Character가 포함된 테이블]")
        for table_name in character_tables:
            print(f"- {table_name}")

        print()
        print("[Item이 포함된 테이블]")
        for table_name in item_tables:
            print(f"- {table_name}")

        print()
        print("[Trait가 포함된 테이블]")
        if trait_tables:
            for table_name in trait_tables:
                print(f"- {table_name}")
        else:
            print("- Trait가 포함된 테이블을 찾지 못했습니다.")

        # 2. 우선 확실히 알려진 주요 테이블 출력
        test_tables = [
            "Character",
            "CharacterLevelUpStat",
            "CharacterMastery",
            "ItemWeapon",
            "ItemArmor",
        ]

        # 3. hash에서 발견한 특성 관련 테이블도 출력 대상에 추가
        test_tables.extend(trait_tables)

        # 같은 테이블 이름이 중복되지 않게 정리
        test_tables = list(dict.fromkeys(test_tables))

        # 실제로 현재 서버에서 제공하는 테이블만 호출
        for meta_type in test_tables:
            if meta_type not in meta_types:
                print()
                print(
                    f"[건너뜀] {meta_type}: "
                    "현재 hash 목록에 없는 테이블입니다."
                )
                continue

            print_table_preview(
                meta_type=meta_type,
                row_limit=ROW_LIMIT,
            )

    except EternalReturnApiError as error:
        print()
        print("===== API 오류 =====", file=sys.stderr)
        print(error, file=sys.stderr)
        return 1

    except Exception as error:
        print()
        print("===== 예상하지 못한 오류 =====", file=sys.stderr)
        print(error, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())