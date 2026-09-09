"""Read-only official game data access; never logs credentials or response bodies."""
from __future__ import annotations

import ipaddress
import math
import re
import threading
import time
from typing import Any
from urllib.parse import urlsplit

import requests

BASE_URL = "https://open-api.bser.io"
TIMEOUT_SECONDS = 15
MIN_REQUEST_INTERVAL_SECONDS = 1.2
MAX_RETRIES = 3
MAX_RETRY_WAIT_SECONDS = 30.0
TRANSIENT_STATUSES = frozenset({429, 500, 502, 503, 504})
TABLE_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_]*\Z")
_last_request_started_at: float | None = None
_request_lock = threading.Lock()


class EternalReturnApiError(RuntimeError):
    """Safe message with no URL, response body, credentials, or request details."""


def get_er_api_key() -> str:
    # Offline imports/tests must not load .env. Only an explicit authenticated call
    # enters the approved settings boundary.
    from settings import get_er_api_key as settings_api_key
    return settings_api_key()


def _wait_for_request_slot() -> None:
    global _last_request_started_at
    with _request_lock:
        now = time.monotonic()
        if _last_request_started_at is not None:
            remaining = MIN_REQUEST_INTERVAL_SECONDS - (now - _last_request_started_at)
            if remaining > 0:
                time.sleep(remaining)
        _last_request_started_at = time.monotonic()


def _get_retry_wait_seconds(response: requests.Response, retry_count: int) -> float:
    delay = 2.0 * retry_count
    try:
        value = float(response.headers.get("Retry-After", ""))
        if math.isfinite(value):
            delay = value
    except (ValueError, TypeError):
        pass
    return min(MAX_RETRY_WAIT_SECONDS, max(MIN_REQUEST_INTERVAL_SECONDS, delay))


def _request(url: str, *, authenticated: bool) -> requests.Response:
    headers = {"Accept": "application/json" if authenticated else "text/plain"}
    if authenticated:
        try:
            headers["x-api-key"] = get_er_api_key()
        except RuntimeError:
            raise EternalReturnApiError("ER_API_KEY is unavailable; live sync was not started.") from None
    for attempt in range(MAX_RETRIES + 1):
        _wait_for_request_slot()
        try:
            response = requests.get(
                url=url, headers=headers, timeout=TIMEOUT_SECONDS,
                allow_redirects=False,
            )
        except requests.Timeout:
            raise EternalReturnApiError("Game data request timed out.") from None
        except requests.RequestException:
            raise EternalReturnApiError("Game data network request failed.") from None
        status = response.status_code
        if status in TRANSIENT_STATUSES and attempt < MAX_RETRIES:
            delay = _get_retry_wait_seconds(response, attempt + 1)
            response.close()
            time.sleep(delay)
            continue
        if not 200 <= status < 300:
            response.close()
            raise EternalReturnApiError(f"Game data request failed (HTTP {status}).") from None
        return response
    raise EternalReturnApiError("Game data retry limit reached.") from None


def _get_payload(path: str) -> Any:
    response = _request(BASE_URL + path, authenticated=True)
    try:
        payload = response.json()
    except ValueError:
        raise EternalReturnApiError("Game data response is not valid JSON.") from None
    finally:
        response.close()
    if not isinstance(payload, dict) or payload.get("code") != 200 or "data" not in payload:
        raise EternalReturnApiError("Game data response has an invalid success envelope.") from None
    return payload["data"]


def get_game_data(meta_type: str) -> list[dict[str, Any]] | dict[str, Any]:
    """GET /v2/data/{metaType}; callers discover table names through hash."""
    if not isinstance(meta_type, str) or not TABLE_NAME.fullmatch(meta_type):
        raise ValueError("Invalid game data table name.")
    data = _get_payload(f"/v2/data/{meta_type}")
    if not isinstance(data, (list, dict)):
        raise EternalReturnApiError("Game data table has an invalid shape.") from None
    return data


def _validate_public_download_url(url: Any) -> str:
    if not isinstance(url, str):
        raise EternalReturnApiError("Korean localization download link is missing.") from None
    try:
        parts = urlsplit(url)
        host = parts.hostname or ""
        if parts.scheme != "https" or not host or parts.username or parts.password or parts.fragment:
            raise ValueError
        if parts.port not in (None, 443) or host.lower() == "localhost" or host.lower().endswith(".localhost"):
            raise ValueError
        try:
            address = ipaddress.ip_address(host)
        except ValueError:
            address = None
        if address is not None and not address.is_global:
            raise ValueError
    except ValueError:
        raise EternalReturnApiError("Korean localization link must be public HTTPS.") from None
    return url


def get_korean_localization() -> str:
    """Official localization metadata then public download without API headers."""
    metadata = _get_payload("/v1/l10n/Korean")
    if not isinstance(metadata, dict):
        raise EternalReturnApiError("Korean localization metadata has an invalid shape.") from None
    url = _validate_public_download_url(metadata.get("l10Path") or metadata.get("l10nPath"))
    response = _request(url, authenticated=False)
    try:
        text = response.content.decode("utf-8-sig")
    except UnicodeDecodeError:
        raise EternalReturnApiError("Korean localization is not UTF-8 text.") from None
    finally:
        response.close()
    if not any("┃" in line for line in text.splitlines()):
        raise EternalReturnApiError("Korean localization contains no documented key/value rows.") from None
    return text
