"""Small environment-file loader used by local inference tools."""

import os
from pathlib import Path


def load_env() -> None:
    """Load repository and inference ``.env`` files without overriding env vars."""
    repo_dir = Path(__file__).resolve().parents[3]
    inference_dir = repo_dir / "inference"
    for path in (repo_dir / ".env", inference_dir / ".env"):
        if not path.is_file():
            continue
        for raw_line in path.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key, value = key.strip(), value.strip()
            if value[:1] in {"'", '"'} and value[-1:] == value[:1]:
                value = value[1:-1]
            os.environ.setdefault(key, value)
