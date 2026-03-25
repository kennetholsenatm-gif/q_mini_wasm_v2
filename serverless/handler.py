"""RunPod Serverless worker handler for qminiwasm training jobs.

Input contract (input dict):
  - qmw_action: "train"
  - config_rel: repo-relative TOML path (e.g. configs/training/wui_working.toml)
  - extra_env: optional ["KEY=value", ...]
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from typing import Any

import runpod


def _as_str(v: Any) -> str:
    return "" if v is None else str(v).strip()


def _apply_extra_env(input_payload: dict[str, Any]) -> None:
    extra_env = input_payload.get("extra_env")
    if not isinstance(extra_env, list):
        return
    for item in extra_env:
        if not isinstance(item, str):
            continue
        s = item.strip()
        if "=" not in s:
            continue
        k, v = s.split("=", 1)
        k = k.strip()
        if not k:
            continue
        os.environ[k] = v


def _repo_root() -> Path:
    # Image default; override with env if your image lays out repo differently.
    root = _as_str(os.getenv("QMW_REPO_ROOT")) or "/app"
    return Path(root)


def _run_train(config_abs: Path) -> tuple[int, str, str]:
    py = _as_str(os.getenv("PYTHON_BIN")) or sys.executable
    cmd = [py, "-m", "engine", "--config", str(config_abs)]
    proc = subprocess.run(
        cmd,
        cwd=str(_repo_root()),
        text=True,
        capture_output=True,
        env=os.environ.copy(),
    )
    return proc.returncode, proc.stdout, proc.stderr


def handler(job: dict[str, Any]) -> dict[str, Any]:
    input_payload = job.get("input") if isinstance(job, dict) else {}
    if not isinstance(input_payload, dict):
        return {"ok": False, "error": "input must be an object"}

    action = _as_str(input_payload.get("qmw_action"))
    if action != "train":
        return {"ok": False, "error": f'unsupported qmw_action "{action}" (expected "train")'}

    config_rel = _as_str(input_payload.get("config_rel"))
    if not config_rel:
        return {"ok": False, "error": 'missing input.config_rel'}

    _apply_extra_env(input_payload)
    repo = _repo_root()
    config_abs = (repo / config_rel).resolve()
    try:
        config_abs.relative_to(repo.resolve())
    except ValueError:
        return {"ok": False, "error": f"config_rel escapes repo root: {config_rel}"}
    if not config_abs.exists():
        return {"ok": False, "error": f"config not found: {config_abs}"}

    code, out, err = _run_train(config_abs)
    return {
        "ok": code == 0,
        "exit_code": code,
        "config_rel": config_rel,
        "stdout": out[-12000:],
        "stderr": err[-12000:],
    }


runpod.serverless.start({"handler": handler})

