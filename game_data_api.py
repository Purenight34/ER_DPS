from __future__ import annotations

from typing import Any

import requests

from settings import get_er_api_key


BASE_URL = "https://open-api.bser.io"
TIMEOUT_SECONDS = 15


class EternalReturnApiError(RuntimeError):
    """이터널 리턴 API 요청 과정에서 발생한 오류."""


def get_game_data(
    meta_type: str,
) -> list[dict[str, Any]] | dict[str, Any]:
    """
    이터널 리턴의 인게임 데이터 테이블을 불러온다.

    사용 예시:
        get_game_data("Character")
        get_game_data("ItemWeapon")
        get_game_data("hash")

    일반 데이터 테이블:
        list[dict] 반환

    hash 테이블:
        dict 반환
    """

    api_key = get_er_api_key()

    url = f"{BASE_URL}/v2/data/{meta_type}"

    headers = {
        "x-api-key": api_key,
        "Accept": "application/json",
    }

    try:
        response = requests.get(
            url=url,
            headers=headers,
            timeout=TIMEOUT_SECONDS,
        )

    except requests.Timeout as error:
        raise EternalReturnApiError(
            f"API 서버가 {TIMEOUT_SECONDS}초 안에 응답하지 않았습니다."
        ) from error

    except requests.RequestException as error:
        raise EternalReturnApiError(
            f"네트워크 요청 중 오류가 발생했습니다: {error}"
        ) from error

    print(f"요청한 테이블: {meta_type}")
    print(f"요청 URL: {response.url}")
    print(f"HTTP 상태 코드: {response.status_code}")

    # HTTP 400, 403, 404, 429, 500 등의 오류 처리
    if not response.ok:
        raise EternalReturnApiError(
            "API 요청에 실패했습니다.\n"
            f"HTTP 상태 코드: {response.status_code}\n"
            f"응답 내용: {response.text[:500]}"
        )

    try:
        payload = response.json()

    except ValueError as error:
        raise EternalReturnApiError(
            "API 응답을 JSON으로 변환하지 못했습니다.\n"
            f"응답 내용: {response.text[:500]}"
        ) from error

    if not isinstance(payload, dict):
        raise EternalReturnApiError(
            "API 응답의 최상위 값이 dict 형식이 아닙니다."
        )

    api_code = payload.get("code")
    api_message = payload.get("message")

    if api_code != 200:
        raise EternalReturnApiError(
            "이터널 리턴 API가 실패 코드를 반환했습니다.\n"
            f"API code: {api_code}\n"
            f"message: {api_message}"
        )

    if "data" not in payload:
        raise EternalReturnApiError(
            "API 응답에 data 필드가 없습니다.\n"
            f"응답 필드: {list(payload.keys())}"
        )

    return payload["data"]


def get_available_meta_types() -> dict[str, Any]:
    """
    현재 API에서 사용할 수 있는 모든 metaType을 가져온다.

    반환 예시:
        {
            "Character": 12345678,
            "ItemWeapon": 87654321,
            ...
        }
    """

    data = get_game_data("hash")

    if not isinstance(data, dict):
        raise EternalReturnApiError(
            "hash 요청의 data가 dict 형식이 아닙니다."
        )

    return data