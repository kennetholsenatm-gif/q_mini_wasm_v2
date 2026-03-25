#!/usr/bin/env python3
"""Run the two-stage STEM+code blend training workflow.

Stage 1: CPU warmup (The Stack smol blend)
Stage 2: GPU continuation (NuminaMath-CoT), resumed from stage-1 latest checkpoint.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PHASE1_CONFIG = REPO_ROOT / "configs" / "training" / "stem_code_blend_phase1_cpu.toml"
DEFAULT_PHASE2_CONFIG = (
    REPO_ROOT / "configs" / "training" / "stem_code_blend_phase2_gpu_resume.toml"
)
DEFAULT_PHASE1_LATEST = (
    REPO_ROOT / "artifacts" / "models" / "stem_code_blend_phase1_cpu" / "latest.pt"
)


def _run_engine(config_path: Path, *, dry_run: bool) -> int:
    cmd = [sys.executable, "-m", "engine", "--config", str(config_path)]
    print(" ".join(cmd))
    if dry_run:
        return 0
    return subprocess.run(cmd, cwd=str(REPO_ROOT), check=False).returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run phase-1 CPU and phase-2 GPU continuation training in sequence.",
    )
    parser.add_argument(
        "--phase1-config",
        default=str(DEFAULT_PHASE1_CONFIG),
        help="Path to phase-1 config TOML.",
    )
    parser.add_argument(
        "--phase2-config",
        default=str(DEFAULT_PHASE2_CONFIG),
        help="Path to phase-2 config TOML.",
    )
    parser.add_argument(
        "--phase1-latest",
        default=str(DEFAULT_PHASE1_LATEST),
        help="Checkpoint that must exist before phase 2 starts.",
    )
    parser.add_argument(
        "--skip-phase1",
        action="store_true",
        help="Skip phase 1 and run only phase 2 (requires --phase1-latest to exist).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print commands and checks without running engine.",
    )
    args = parser.parse_args()

    phase1_config = Path(args.phase1_config).expanduser()
    phase2_config = Path(args.phase2_config).expanduser()
    phase1_latest = Path(args.phase1_latest).expanduser()
    if not phase1_config.is_absolute():
        phase1_config = (REPO_ROOT / phase1_config).resolve()
    if not phase2_config.is_absolute():
        phase2_config = (REPO_ROOT / phase2_config).resolve()
    if not phase1_latest.is_absolute():
        phase1_latest = (REPO_ROOT / phase1_latest).resolve()

    print(f"repo root: {REPO_ROOT}")
    print(f"phase1 config: {phase1_config}")
    print(f"phase2 config: {phase2_config}")
    print(f"phase1 latest checkpoint: {phase1_latest}")

    if not args.skip_phase1:
        rc = _run_engine(phase1_config, dry_run=args.dry_run)
        if rc != 0:
            print(f"phase 1 failed with code {rc}", file=sys.stderr)
            return rc

    if not args.dry_run and not phase1_latest.is_file():
        print(
            f"phase 2 aborted: expected checkpoint not found: {phase1_latest}",
            file=sys.stderr,
        )
        return 2

    if args.dry_run and args.skip_phase1:
        print("dry-run: would require existing phase1 checkpoint before phase 2.")

    rc = _run_engine(phase2_config, dry_run=args.dry_run)
    if rc != 0:
        print(f"phase 2 failed with code {rc}", file=sys.stderr)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
