"""Print whether common API tokens are present (never print values). Run from repo root."""

from __future__ import annotations

import os
import sys
from pathlib import Path

# Repo root = parent of scripts/
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from qminiwasm.engine._dotenv import load_dotenv_if_available  # noqa: E402


def _present(key: str) -> bool:
    return bool(os.environ.get(key, "").strip())


def main() -> int:
    os.chdir(ROOT)
    load_dotenv_if_available()
    dotenv_path = ROOT / ".env"
    print(f"repo_root: {ROOT}")
    print(f".env file exists: {dotenv_path.is_file()}")
    if dotenv_path.is_file():
        keys: list[str] = []
        for raw in dotenv_path.read_text(encoding="utf-8", errors="ignore").splitlines():
            s = raw.strip()
            if not s or s.startswith("#") or "=" not in s:
                continue
            keys.append(s.split("=", 1)[0].strip())
        print(f".env keys: {', '.join(keys) if keys else '(none)'}")
    print(f"IBM_QUANTUM_API_TOKEN present: {_present('IBM_QUANTUM_API_TOKEN')}")
    print(f"QISKIT_IBM_TOKEN present: {_present('QISKIT_IBM_TOKEN')}")
    print(f"HF_TOKEN present: {_present('HF_TOKEN')}")
    print(f"HUGGING_FACE_HUB_TOKEN present: {_present('HUGGING_FACE_HUB_TOKEN')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
