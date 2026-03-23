#!/usr/bin/env python3
"""One-command resume: same as ``run_training_cascade_mopd.py --resume`` (load latest .pt, run engine).

Uses ``artifacts/qminiwasm_latest_cascade_mopd.pt`` or ``CHECKPOINT_LATEST_PATH`` from ``.env``.
Pass through extra args, e.g. ``python scripts/resume_training_cascade_mopd.py --dry-run``.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
RUNNER = REPO_ROOT / "scripts" / "run_training_cascade_mopd.py"


def main() -> int:
    cmd = [sys.executable, str(RUNNER), "--resume", *sys.argv[1:]]
    return subprocess.run(cmd, cwd=str(REPO_ROOT), check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
