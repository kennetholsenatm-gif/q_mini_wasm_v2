"""Quick benchmark harness for edge curriculum presets."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path


def run_one(cfg: Path) -> dict:
    t0 = time.perf_counter()
    p = subprocess.run(
        [sys.executable, "-m", "engine", "--config", str(cfg)],
        capture_output=True,
        text=True,
        check=False,
    )
    dt = time.perf_counter() - t0
    out = p.stdout or ""
    err = p.stderr or ""
    return {
        "config": str(cfg),
        "returncode": int(p.returncode),
        "elapsed_seconds": dt,
        "stdout_tail": out[-2000:],
        "stderr_tail": err[-1200:],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--configs",
        nargs="+",
        default=[
            "configs/training/edge_fast_iter.toml",
            "configs/training/edge_full_curriculum_streaming.toml",
        ],
    )
    args = ap.parse_args()
    results = [run_one(Path(c)) for c in args.configs]
    print(json.dumps({"results": results}, indent=2))
    bad = [r for r in results if r["returncode"] != 0]
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())

