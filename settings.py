from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv

# settings.py가 있는 폴더를 프로젝트 루트로 사용
PROJECT_ROOT = Path(__file__).resolve().parent
ENV_PATH = PROJECT_ROOT / ".env"

load_dotenv(
    dotenv_path=ENV_PATH,
    override=False,
)

def get_er_api_key() -> str:
    """
    환경변수에서 이터널 리턴 API 키를 가져온다.

    키가 없거나 빈 문자열이면 오류를 발생시킨다.
    """

    api_key = os.getenv("ER_API_KEY", "").strip()

    if not api_key:
        raise RuntimeError(
            "ER_API_KEY를 찾을 수 없습니다.\n"
            f".env 파일 위치를 확인하세요: {ENV_PATH}"
        )

    return api_key