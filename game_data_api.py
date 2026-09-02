from __future__ import annotations

import time
from typing import Any

import requests

from settings import get_er_api_key


BASE_URL = "https://open-api.bser.io"
TIMEOUT_SECONDS = 15

# 초당 1회 제한을 넘지 않도록 요청 시작 사이에 둘 간격
# 1.0초에 정확히 맞추기보다 약간 여유를 둔다.
MIN_REQUEST_INTERVAL_SECONDS = 1.2

# 429 발생 시 최대 재시도 횟수
MAX_RETRIES = 3

# 현재 Python 프로그램 안에서 마지막 요청을 시작한 시각
_last_request_started_at: float | None = None


class EternalReturnApiError(RuntimeError):
    """이터널 리턴 API 호출 중 발생한 오류."""


def _wait_for_request_slot() -> None:
    """
    이전 API 요청을 시작한 시각으로부터 최소 1.2초가 지나도록 기다린다.

    time.monotonic()은 시스템 시계가 변경되더라도
    경과 시간을 안정적으로 계산하기 위해 사용한다.
    """

    global _last_request_started_at

    current_time = time.monotonic()

    if _last_request_started_at is not None:
        elapsed_time = current_time - _last_request_started_at
        remaining_time = (
            MIN_REQUEST_INTERVAL_SECONDS - elapsed_time
        )

        if remaining_time > 0:
            print(
                f"호출 제한을 지키기 위해 "
                f"{remaining_time:.2f}초 기다립니다."
            )
            time.sleep(remaining_time)

    # 다음 요청을 바로 시작할 예정이므로 현재 시각 기록
    _last_request_started_at = time.monotonic()


def _get_retry_wait_seconds(
    response: requests.Response,
    retry_count: int,
) -> float:
    """
    429 응답이 발생했을 때 기다릴 시간을 계산한다.

    서버가 Retry-After 헤더를 주면 해당 값을 사용하고,
    없으면 2초, 4초, 6초 순서로 기다린다.
    """

    retry_after = response.headers.get("Retry-After")

    if retry_after is not None:
        try:
            return max(
                float(retry_after),
                MIN_REQUEST_INTERVAL_SECONDS,
            )
        except ValueError:
            pass

    return 2.0 * retry_count


def get_game_data(
    meta_type: str,
) -> list[dict[str, Any]] | dict[str, Any]:
    """
    이터널 리턴 인게임 데이터 테이블 하나를 가져온다.

    예시:
        get_game_data("hash")
        get_game_data("Character")
        get_game_data("ItemWeapon")

    반환:
        일반 테이블:
            list[dict]

        hash:
            dict
    """

    meta_type = meta_type.strip()

    if not meta_type:
        raise ValueError("meta_type이 비어 있습니다.")

    api_key = get_er_api_key()
    url = f"{BASE_URL}/v2/data/{meta_type}"

    headers = {
        "x-api-key": api_key,
        "Accept": "application/json",
    }

    # 최초 요청 1회 + 재시도 최대 3회
    for attempt in range(MAX_RETRIES + 1):
        _wait_for_request_slot()

        try:
            response = requests.get(
                url=url,
                headers=headers,
                timeout=TIMEOUT_SECONDS,
            )

        except requests.Timeout as error:
            raise EternalReturnApiError(
                f"API 서버가 {TIMEOUT_SECONDS}초 안에 "
                "응답하지 않았습니다."
            ) from error

        except requests.RequestException as error:
            raise EternalReturnApiError(
                f"네트워크 요청 중 오류가 발생했습니다: {error}"
            ) from error

        print(f"요청 테이블: {meta_type}")
        print(f"요청 URL: {response.url}")
        print(f"HTTP 상태 코드: {response.status_code}")

        # 호출 제한 발생
        if response.status_code == 429:
            if attempt >= MAX_RETRIES:
                raise EternalReturnApiError(
                    "호출 제한으로 API 요청에 실패했습니다.\n"
                    f"{MAX_RETRIES}회 재시도했지만 "
                    "계속 HTTP 429가 반환됐습니다."
                )

            retry_count = attempt + 1
            wait_seconds = _get_retry_wait_seconds(
                response=response,
                retry_count=retry_count,
            )

            print(
                "HTTP 429: Too Many Requests\n"
                f"{wait_seconds:.1f}초 후 다시 요청합니다. "
                f"({retry_count}/{MAX_RETRIES})"
            )

            time.sleep(wait_seconds)
            continue

        # 400, 403, 404, 500 등의 다른 HTTP 오류
        if not response.ok:
            raise EternalReturnApiError(
                "API 요청에 실패했습니다.\n"
                f"HTTP 상태 코드: {response.status_code}\n"
                f"응답 내용:\n{response.text[:500]}"
            )

        try:
            payload = response.json()

        except ValueError as error:
            raise EternalReturnApiError(
                "API 응답을 JSON으로 변환하지 못했습니다.\n"
                f"응답 내용:\n{response.text[:500]}"
            ) from error

        if not isinstance(payload, dict):
            raise EternalReturnApiError(
                "API 응답의 최상위 값이 dict가 아닙니다."
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

    # 정상적인 흐름에서는 도달하지 않음
    raise EternalReturnApiError(
        "알 수 없는 이유로 API 요청에 실패했습니다."
    )