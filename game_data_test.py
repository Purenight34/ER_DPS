from __future__ import annotations

import json
import sys
from typing import Any

from game_data_api import (
    EternalReturnApiError,
    get_game_data,
)


# 여기만 바꾸면서 테이블을 하나씩 확인한다.
META_TYPE = "Character"

# 앞에서 몇 행까지 출력할지
ROW_LIMIT = 2


def print_game_data(
    meta_type: str,
    data: list[dict[str, Any]] | dict[str, Any],
) -> None:
    print()
    print("=" * 70)
    print(f"{meta_type} 응답 결과")
    print("=" * 70)

    # Character, ItemWeapon 같은 일반 테이블
    if isinstance(data, list):
        print("Python 자료형: list")
        print(f"전체 행 개수: {len(data)}")

        if not data:
            print("데이터가 비어 있습니다.")
            return

        first_row = data[0]

        if isinstance(first_row, dict):
            print()
            print("첫 번째 행의 필드:")
            print(list(first_row.keys()))

        print()
        print(
            f"앞에서부터 "
            f"{min(ROW_LIMIT, len(data))}개 행을 출력합니다."
        )

        for index, row in enumerate(
            data[:ROW_LIMIT],
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

    # hash 테이블
    if isinstance(data, dict):
        print("Python 자료형: dict")
        print(f"전체 항목 개수: {len(data)}")

        print()
        print(f"앞에서부터 {ROW_LIMIT}개 항목:")

        for index, (key, value) in enumerate(
            data.items()
        ):
            if index >= ROW_LIMIT:
                break

            print(f"{key}: {value}")

        return

    print(f"예상하지 못한 자료형: {type(data)}")
    print(data)


def main() -> int:
    try:
        data = get_game_data(META_TYPE)
        print_game_data(META_TYPE, data)

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