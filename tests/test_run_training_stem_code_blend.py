from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "scripts" / "run_training_stem_code_blend.py"


def test_stem_blend_runner_dry_run_shows_both_phase_commands():
    result = subprocess.run(
        [sys.executable, str(SCRIPT), "--dry-run"],
        cwd=str(REPO_ROOT),
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0
    assert "stem_code_blend_phase1_cpu.toml" in result.stdout
    assert "stem_code_blend_phase2_gpu_resume.toml" in result.stdout


def test_stem_blend_runner_skip_phase1_requires_checkpoint(tmp_path):
    cfg2 = tmp_path / "phase2.toml"
    cfg2.write_text("[training]\nepochs = 1\n", encoding="utf-8")
    missing_ckpt = tmp_path / "missing_latest.pt"
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--skip-phase1",
            "--phase2-config",
            str(cfg2),
            "--phase1-latest",
            str(missing_ckpt),
        ],
        cwd=str(REPO_ROOT),
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert "expected checkpoint not found" in result.stderr
