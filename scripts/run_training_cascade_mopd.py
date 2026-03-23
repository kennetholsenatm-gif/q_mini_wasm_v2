#!/usr/bin/env python3
"""Inject env for the Cascade RL + MOPD continuation run, then start ``python -m engine``.

Loads repo-root ``.env`` first (if present and python-dotenv is installed), then sets
Cascade/MOPD and checkpoint variables so they override ``.env`` for this process.

See docs/CASCADE_AND_MOPD.md (Recommended continuation run; § Long runs) and .env.example.

For tmux / checkpoint continuation (load latest after a stop), see CASCADE_AND_MOPD.md.

Usage::

    python scripts/run_training_cascade_mopd.py
    python scripts/run_training_cascade_mopd.py --mopd-lambda 0.05 --checkpoint-load ./artifacts/best.pt
    python scripts/run_training_cascade_mopd.py --dry-run
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def _load_dotenv_repo() -> None:
    try:
        from dotenv import load_dotenv
    except ImportError:
        return
    env_path = REPO_ROOT / ".env"
    if env_path.is_file():
        load_dotenv(env_path, override=False)


def _apply_injections(
    *,
    mopd_lambda: str,
    mopd_feat_loss: str,
    checkpoint_load: str | None,
    checkpoint_save: str | None,
    checkpoint_best: str | None,
    checkpoint_latest: str | None,
    use_cascade_router: bool,
    learned_projector: bool,
    cascade_steps: int | None,
    eval_holdout: float | None,
    eval_every_epoch: bool,
    accelerator: str | None,
) -> dict[str, str]:
    """Return key-value pairs to merge into the environment (all string values)."""
    out: dict[str, str] = {
        "CASCADE_MOPD_LAMBDA": mopd_lambda,
        "CASCADE_MOPD_FEAT_LOSS": mopd_feat_loss,
    }
    if cascade_steps is not None:
        out["CASCADE_STEPS_PER_EPOCH"] = str(cascade_steps)
    if checkpoint_load:
        out["CHECKPOINT_LOAD_PATH"] = checkpoint_load
    if checkpoint_save:
        out["CHECKPOINT_SAVE_PATH"] = checkpoint_save
    if checkpoint_best:
        out["CHECKPOINT_BEST_PATH"] = checkpoint_best
    if checkpoint_latest:
        out["CHECKPOINT_LATEST_PATH"] = checkpoint_latest
    if use_cascade_router:
        out["USE_CASCADE_ROUTER"] = "1"
    if learned_projector:
        out["CASCADE_LEARNED_PROJECTOR"] = "1"
    if eval_holdout is not None:
        out["EVAL_HOLDOUT_FRACTION"] = str(eval_holdout)
    if eval_every_epoch:
        out["EVAL_EVERY_EPOCH"] = "1"
    if accelerator:
        out["ACCELERATOR"] = accelerator
    return out


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run training with Cascade RL + MOPD env injected (after .env).",
    )
    parser.add_argument(
        "--mopd-lambda",
        default="0.1",
        help="CASCADE_MOPD_LAMBDA (default: 0.1). Try 0.05–0.1 first.",
    )
    parser.add_argument(
        "--mopd-feat-loss",
        choices=("mse", "cosine"),
        default="mse",
        help="CASCADE_MOPD_FEAT_LOSS (default: mse).",
    )
    parser.add_argument(
        "--checkpoint-load",
        default=None,
        help="CHECKPOINT_LOAD_PATH (best baseline .pt for warm start).",
    )
    parser.add_argument(
        "--checkpoint-save",
        default=str(REPO_ROOT / "artifacts" / "qminiwasm_trainable_cascade_mopd.pt"),
        help="CHECKPOINT_SAVE_PATH (default: artifacts/qminiwasm_trainable_cascade_mopd.pt).",
    )
    parser.add_argument(
        "--checkpoint-best",
        default=str(REPO_ROOT / "artifacts" / "qminiwasm_best_cascade_mopd.pt"),
        help="CHECKPOINT_BEST_PATH (default: artifacts/qminiwasm_best_cascade_mopd.pt).",
    )
    parser.add_argument(
        "--checkpoint-latest",
        default=str(REPO_ROOT / "artifacts" / "qminiwasm_latest_cascade_mopd.pt"),
        help="CHECKPOINT_LATEST_PATH (default: artifacts/qminiwasm_latest_cascade_mopd.pt).",
    )
    parser.add_argument(
        "--no-checkpoint-outputs",
        action="store_true",
        help="Do not set CHECKPOINT_SAVE_PATH / BEST / LATEST (use .env only).",
    )
    parser.add_argument(
        "--use-cascade-router",
        action="store_true",
        help="Set USE_CASCADE_ROUTER=1 for training (aligns with cascade_logits on serve).",
    )
    parser.add_argument(
        "--learned-projector",
        action="store_true",
        help="Set CASCADE_LEARNED_PROJECTOR=1 (loop-owned router; no model cascade_router).",
    )
    parser.add_argument(
        "--cascade-steps",
        type=int,
        default=None,
        metavar="N",
        help="Override CASCADE_STEPS_PER_EPOCH (e.g. 3).",
    )
    parser.add_argument(
        "--eval-holdout",
        type=float,
        default=None,
        metavar="FRACTION",
        help="Set EVAL_HOLDOUT_FRACTION (e.g. 0.05).",
    )
    parser.add_argument(
        "--eval-every-epoch",
        action="store_true",
        help="Set EVAL_EVERY_EPOCH=1 (use with --eval-holdout).",
    )
    parser.add_argument(
        "--accelerator",
        default=None,
        help="Override ACCELERATOR (e.g. xpu, cpu, cuda).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print injected variables and exit without running the engine.",
    )
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    _load_dotenv_repo()

    save = None if args.no_checkpoint_outputs else args.checkpoint_save
    best = None if args.no_checkpoint_outputs else args.checkpoint_best
    latest = None if args.no_checkpoint_outputs else args.checkpoint_latest

    injections = _apply_injections(
        mopd_lambda=args.mopd_lambda,
        mopd_feat_loss=args.mopd_feat_loss,
        checkpoint_load=args.checkpoint_load,
        checkpoint_save=save,
        checkpoint_best=best,
        checkpoint_latest=latest,
        use_cascade_router=args.use_cascade_router,
        learned_projector=args.learned_projector,
        cascade_steps=args.cascade_steps,
        eval_holdout=args.eval_holdout,
        eval_every_epoch=args.eval_every_epoch,
        accelerator=args.accelerator,
    )

    env = os.environ.copy()
    env.update(injections)

    print("run_training_cascade_mopd: repo root", REPO_ROOT)
    print("Injected / overridden for this run:")
    for k in sorted(injections):
        print(f"  {k}={injections[k]}")

    if args.dry_run:
        return 0

    return subprocess.run(
        [sys.executable, "-m", "engine"],
        cwd=str(REPO_ROOT),
        env=env,
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
