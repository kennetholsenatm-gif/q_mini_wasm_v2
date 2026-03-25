#!/usr/bin/env python3
"""Inject env for the Cascade RL + MOPD continuation run, then start ``python -m engine``.

Loads repo-root ``.env`` first (if present and python-dotenv is installed), then sets
Cascade/MOPD and checkpoint variables so they override ``.env`` for this process.

See docs/CASCADE_AND_MOPD.md (Recommended continuation run; § Long runs) and .env.example.

For tmux / checkpoint continuation (load latest after a stop), see CASCADE_AND_MOPD.md.

Usage::

    python scripts/run_training_cascade_mopd.py
    python scripts/run_training_cascade_mopd.py --config configs/training/cascade_mopd.toml
    python scripts/run_training_cascade_mopd.py --mopd-lambda 0.05 --checkpoint-load ./artifacts/models/cascade_mopd/best.pt
    python scripts/run_training_cascade_mopd.py --dry-run
    python scripts/run_training_cascade_mopd.py --resume
    python scripts/resume_training_cascade_mopd.py
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
_DEFAULT_CKPT_DIR = REPO_ROOT / "artifacts" / "models" / "cascade_mopd"


def _resolve_latest_checkpoint_path(checkpoint_latest_cli: str) -> Path:
    """Path to the latest .pt file for --resume (CLI path, then CHECKPOINT_LATEST_PATH from env)."""
    p = Path(checkpoint_latest_cli).expanduser()
    if not p.is_absolute():
        p = (REPO_ROOT / p).resolve()
    else:
        p = p.resolve()
    if p.is_file():
        return p
    ev = os.environ.get("CHECKPOINT_LATEST_PATH", "").strip()
    tried2 = ""
    if ev:
        e = Path(ev).expanduser()
        if not e.is_absolute():
            e = (REPO_ROOT / e).resolve()
        else:
            e = e.resolve()
        tried2 = f" and {e}"
        if e.is_file():
            return e
    raise FileNotFoundError(
        f"No checkpoint file for resume. Tried: {p!s}{tried2}. "
        "Train once with CHECKPOINT_LATEST_PATH set, or pass --checkpoint-latest PATH."
    )


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
    resume_group = parser.add_mutually_exclusive_group()
    resume_group.add_argument(
        "--resume",
        action="store_true",
        help="Set CHECKPOINT_LOAD_PATH to the latest checkpoint (--checkpoint-latest or CHECKPOINT_LATEST_PATH in .env).",
    )
    resume_group.add_argument(
        "--checkpoint-load",
        default=None,
        help="CHECKPOINT_LOAD_PATH (best baseline .pt for warm start).",
    )
    parser.add_argument(
        "--checkpoint-save",
        default=str(_DEFAULT_CKPT_DIR / "final.pt"),
        help="CHECKPOINT_SAVE_PATH (default: artifacts/models/cascade_mopd/final.pt).",
    )
    parser.add_argument(
        "--checkpoint-best",
        default=str(_DEFAULT_CKPT_DIR / "best.pt"),
        help="CHECKPOINT_BEST_PATH (default: artifacts/models/cascade_mopd/best.pt).",
    )
    parser.add_argument(
        "--checkpoint-latest",
        default=str(_DEFAULT_CKPT_DIR / "latest.pt"),
        help="CHECKPOINT_LATEST_PATH (default: artifacts/models/cascade_mopd/latest.pt).",
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
    parser.add_argument(
        "--config",
        default=str(REPO_ROOT / "configs" / "training" / "cascade_mopd.toml"),
        metavar="PATH",
        help="Training TOML passed to python -m engine --config (default: configs/training/cascade_mopd.toml).",
    )
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    _load_dotenv_repo()

    load_path = args.checkpoint_load
    if args.resume:
        if args.dry_run:
            try:
                load_path = str(_resolve_latest_checkpoint_path(args.checkpoint_latest))
            except FileNotFoundError:
                p = Path(args.checkpoint_latest).expanduser()
                p = (REPO_ROOT / p).resolve() if not p.is_absolute() else p.resolve()
                load_path = str(p)
                print(
                    "resume dry-run: latest file not found; would set CHECKPOINT_LOAD_PATH=",
                    load_path,
                    file=sys.stderr,
                )
        else:
            try:
                load_path = str(_resolve_latest_checkpoint_path(args.checkpoint_latest))
            except FileNotFoundError as e:
                print(e, file=sys.stderr)
                return 2

    save = None if args.no_checkpoint_outputs else args.checkpoint_save
    best = None if args.no_checkpoint_outputs else args.checkpoint_best
    latest = None if args.no_checkpoint_outputs else args.checkpoint_latest

    injections = _apply_injections(
        mopd_lambda=args.mopd_lambda,
        mopd_feat_loss=args.mopd_feat_loss,
        checkpoint_load=load_path,
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

    cfg_path = Path(args.config).expanduser()
    if not cfg_path.is_absolute():
        cfg_path = (REPO_ROOT / cfg_path).resolve()
    else:
        cfg_path = cfg_path.resolve()

    print("run_training_cascade_mopd: repo root", REPO_ROOT)
    print("Training config:", cfg_path)
    print("Injected / overridden for this run:")
    for k in sorted(injections):
        print(f"  {k}={injections[k]}")

    if args.dry_run:
        return 0

    return subprocess.run(
        [sys.executable, "-m", "engine", "--config", str(cfg_path)],
        cwd=str(REPO_ROOT),
        env=env,
        check=False,
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
