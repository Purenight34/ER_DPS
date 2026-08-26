from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv


# settings.py가 있는 폴더를 프로젝트 루트로 사용
PROJECT_ROOT = Path(__file__).resolve().parent

# 프로젝트 루트의 .env 경로
ENV_PATH = PROJECT_ROOT / ".env"

# .env의 내용을 현재 Python 프로세스 환경변수에 등록
#
# override=False:
# 이미 운영체제 환경변수에 ER_API_KEY가 설정돼 있다면
# .env 값으로 덮어쓰지 않는다.
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