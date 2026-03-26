"""Strip accidental wrappers around secrets (quotes, braces) from env / .env files."""

from __future__ import annotations

import re
from typing import Optional

# IBM / HF and similar reject keys that still have "{", "}", or stray quotes.
_SECRET_ENV_KEY = re.compile(
    r"(TOKEN|API_KEY|APIKEY|SECRET|IBM_QUANTUM|QISKIT|HUGGING_FACE|HF_|RUNPOD)",
    re.IGNORECASE,
)


def is_likely_secret_env_key(name: str) -> bool:
    return bool(_SECRET_ENV_KEY.search(name or ""))


def sanitize_api_key_like(value: Optional[str]) -> Optional[str]:
    """Remove surrounding ``"``, ``'``, or ``{}`` repeatedly until stable.

    Providers often error if the value was pasted as ``{"apikey"}`` or ``"token"``.
    """
    if value is None:
        return None
    s = str(value).strip()
    if not s:
        return None
    while True:
        t = s.strip()
        if len(t) < 2:
            break
        if (t[0] == '"' and t[-1] == '"') or (t[0] == "'" and t[-1] == "'"):
            s = t[1:-1].strip()
            continue
        if t[0] == "{" and t[-1] == "}":
            s = t[1:-1].strip()
            continue
        break
    return s or None


def sanitize_secret_environ() -> None:
    """Normalize likely-secret entries in ``os.environ`` in place (after ``load_dotenv``)."""
    import os

    for k in list(os.environ.keys()):
        if not is_likely_secret_env_key(k):
            continue
        v = os.environ.get(k)
        if v is None:
            continue
        cleaned = sanitize_api_key_like(v)
        if cleaned != v:
            os.environ[k] = cleaned if cleaned is not None else ""
