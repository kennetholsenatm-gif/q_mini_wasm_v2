#!/usr/bin/env python3
"""Install PyTorch XPU wheels from download.pytorch.org (replaces CPU/CUDA torch in the active venv).

Usage:
  python scripts/install_torch_xpu.py           # stable XPU index
  python scripts/install_torch_xpu.py --nightly
  python scripts/install_torch_xpu.py --dry-run
"""

from __future__ import annotations

import argparse
import subprocess
import sys

STABLE_XPU_INDEX = "https://download.pytorch.org/whl/xpu"
NIGHTLY_XPU_INDEX = "https://download.pytorch.org/whl/nightly/xpu"


def _run(cmd: list[str], dry_run: bool) -> None:
    print("+", " ".join(cmd))
    if dry_run:
        return
    subprocess.run(cmd, check=True)


def main() -> int:
    p = argparse.ArgumentParser(description="Install torch/torchvision/torchaudio from PyTorch XPU index.")
    p.add_argument("--nightly", action="store_true", help="Use nightly/xpu index and --pre.")
    p.add_argument("--dry-run", action="store_true", help="Print commands only.")
    args = p.parse_args()

    index = NIGHTLY_XPU_INDEX if args.nightly else STABLE_XPU_INDEX
    pip = [sys.executable, "-m", "pip"]

    _run(pip + ["uninstall", "-y", "torch", "torchvision", "torchaudio"], args.dry_run)

    install = pip + ["install"]
    if args.nightly:
        install.append("--pre")
    install += [
        "torch",
        "torchvision",
        "torchaudio",
        "--index-url",
        index,
    ]
    _run(install, args.dry_run)

    if not args.dry_run:
        print("Done. Run: python scripts/verify_torch_xpu.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
