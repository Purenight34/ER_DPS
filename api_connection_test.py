from __future__ import annotations

import json
import sys

import requests

from settings import get_er_api_key


API_URL = "https://open-api.bser.io/v1/user/nickname"
USER_NICKNAME = "tesed"
TIMEOUT_SECONDS = 15


def request_user_by_nickname(
    nickname: str,
) -> dict:
    api_key = get_er_api_key()

    response = requests.get(
        API_URL,
        headers={
            "x-api-key": api_key,
            "Accept": "application/json",
        },
        params={
            "query": nickname,
        },
        timeout=TIMEOUT_SECONDS,
    )

    print(f"실제 요청 URL: {response.url}")
    print(f"HTTP 상태 코드: {response.status_code}")

    if not response.ok:
        raise RuntimeError(
            "API 요청에 실패했습니다.\n"
            f"HTTP 상태 코드: {response.status_code}\n"
            f"응답 내용: {response.text[:500]}"
        )

    payload = response.json()

    if payload.get("code") != 200:
        raise RuntimeError(
            "API가 실패 코드를 반환했습니다.\n"
            f"API code: {payload.get('code')}\n"
            f"message: {payload.get('message')}"
        )

    return payload


def main() -> int:
    try:
        payload = request_user_by_nickname(USER_NICKNAME)

        print()
        print("===== 전체 응답 =====")
        print(
            json.dumps(
                payload,
                ensure_ascii=False,
                indent=2,
            )
        )

        user = payload.get("user")

        if not isinstance(user, dict):
            print()
            print("닉네임에 해당하는 유저 정보를 찾지 못했습니다.")
            return 1

        print()
        print("===== 유저 정보 =====")
        print(f"유저 번호: {user.get('userNum')}")
        print(f"닉네임   : {user.get('nickname')}")

        return 0

    except requests.Timeout:
        print(
            f"API 서버가 {TIMEOUT_SECONDS}초 안에 응답하지 않았습니다.",
            file=sys.stderr,
        )
        return 1

    except requests.RequestException as error:
        print(f"네트워크 오류: {error}", file=sys.stderr)
        return 1

    except (RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())