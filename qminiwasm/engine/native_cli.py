"""Argv builder for the native training CLI ``qmw-grpc-train`` (Go, training-wui/cmd)."""

from __future__ import annotations

import os
import shutil
from pathlib import Path


def qmw_grpc_train_argv(*, repo_root: Path, config_path: Path) -> list[str]:
    exe = (os.environ.get("QMW_GRPC_TRAIN_BIN") or "qmw-grpc-train").strip() or "qmw-grpc-train"
    grpc = (os.environ.get("QMINIWASM_TRAINING_GRPC_ADDR") or "127.0.0.1:50061").strip()
    root = repo_root.resolve()
    cfg_abs = config_path.resolve()
    try:
        cfg_arg = str(cfg_abs.relative_to(root))
    except ValueError:
        cfg_arg = str(cfg_abs)
    return [exe, "-root", str(root), "-config", cfg_arg, "-grpc", grpc]


def qmw_grpc_train_executable() -> str | None:
    """Return path or name on PATH for ``qmw-grpc-train``, or None if not found."""
    exe = (os.environ.get("QMW_GRPC_TRAIN_BIN") or "").strip()
    if exe:
        p = Path(exe)
        if p.is_file():
            return str(p)
        w = shutil.which(exe)
        return w
    return shutil.which("qmw-grpc-train")
