#!/usr/bin/env python3
"""Autonomous learning-rate tuner for training TOML configs.

Runs short training trials across candidate learning rates, tracks runtime and metric,
captures reproducibility metadata, and recommends/promotes LR vs baseline.
"""

from __future__ import annotations

import argparse
import ast
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib  # type: ignore[no-redef,import-untyped]


REPO_ROOT = Path(__file__).resolve().parents[1]


@dataclass
class TrialResult:
    lr: float
    ok: bool
    elapsed_s: float
    metric_name: str
    metric_value: float | None
    epochs_completed: int | None
    exit_code: int
    timed_out: bool
    stderr_tail: str


def _parse_candidates(raw: str) -> list[float]:
    vals: list[float] = []
    for x in raw.split(","):
        x = x.strip()
        if not x:
            continue
        vals.append(float(x))
    if not vals:
        raise ValueError("no learning-rate candidates provided")
    return vals


def _replace_training_scalar(toml_text: str, key: str, value: str) -> str:
    lines = toml_text.splitlines()
    out: list[str] = []
    in_training = False
    found = False
    for ln in lines:
        stripped = ln.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_training and not found:
                out.append(f"{key} = {value}")
                found = True
            in_training = stripped == "[training]"
            out.append(ln)
            continue
        if in_training and stripped.startswith(f"{key}"):
            out.append(f"{key} = {value}")
            found = True
        else:
            out.append(ln)
    if in_training and not found:
        out.append(f"{key} = {value}")
        found = True
    if not found:
        out.extend(["", "[training]", f"{key} = {value}"])
    return "\n".join(out) + "\n"


def _extract_training_result(stdout: str) -> dict[str, Any] | None:
    marker = "Training complete:"
    idx = stdout.rfind(marker)
    if idx < 0:
        return None
    tail = stdout[idx + len(marker) :].strip()
    if not tail:
        return None
    first_line = tail.splitlines()[0].strip()
    try:
        parsed = ast.literal_eval(first_line)
    except Exception:
        return None
    if isinstance(parsed, dict):
        return parsed
    return None


def _metric_from_result(result: dict[str, Any] | None) -> tuple[str, float | None]:
    if not isinstance(result, dict):
        return ("none", None)
    metrics = result.get("metrics") if isinstance(result.get("metrics"), dict) else {}
    if isinstance(metrics, dict):
        ev = metrics.get("eval_mean_mse")
        if isinstance(ev, (int, float)):
            return ("eval_mean_mse", float(ev))
    v = result.get("final_loss")
    if isinstance(v, (int, float)):
        return ("final_loss", float(v))
    return ("none", None)


def _metric_from_logs(text: str) -> tuple[str, float | None]:
    # Prefer holdout metric if available.
    eval_matches = re.findall(r"eval_mean_mse=([0-9eE+\-.]+)", text)
    if eval_matches:
        try:
            return ("eval_mean_mse_partial", float(eval_matches[-1]))
        except ValueError:
            pass
    train_matches = re.findall(r"mean_mse=([0-9eE+\-.]+)", text)
    if train_matches:
        try:
            return ("mean_mse_partial", float(train_matches[-1]))
        except ValueError:
            pass
    return ("none", None)


def _run_trial(config_path: Path, timeout_s: int) -> tuple[int, str, str, float]:
    cmd = [sys.executable, "-m", "qminiwasm.engine", "--config", str(config_path)]
    t0 = time.perf_counter()
    try:
        cp = subprocess.run(
            cmd,
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=timeout_s,
            check=False,
            env=os.environ.copy(),
        )
        return (cp.returncode, cp.stdout, cp.stderr, time.perf_counter() - t0)
    except subprocess.TimeoutExpired as e:

        def _as_text(v: Any) -> str:
            if v is None:
                return ""
            if isinstance(v, bytes):
                return v.decode("utf-8", errors="replace")
            return str(v)

        out = _as_text(e.stdout)
        err = _as_text(e.stderr) + f"\n[timeout after {timeout_s}s]"
        return (124, out, err, time.perf_counter() - t0)


def _write_metadata(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="Autonomous LR tuner (autoresearch-inspired).")
    ap.add_argument("--base-config", required=True, help="Path to base training TOML")
    ap.add_argument("--candidate-lrs", default="3e-5,1e-4,3e-4", help="Comma-separated LR list")
    ap.add_argument("--trial-epochs", type=int, default=3, help="Epochs per trial")
    ap.add_argument(
        "--max-runtime-seconds", type=int, default=180, help="Per-trial timeout seconds"
    )
    ap.add_argument(
        "--metadata-out",
        default="",
        help="Optional metadata JSON path (default artifacts/lr_tuning/...)",
    )
    ap.add_argument(
        "--recommended-config-out",
        default="",
        help="Optional tuned config output path (default artifacts/lr_tuning/...)",
    )
    ap.add_argument("--json", action="store_true", help="Print final summary as JSON")
    args = ap.parse_args()

    base_cfg = Path(args.base_config).expanduser()
    if not base_cfg.is_absolute():
        base_cfg = (REPO_ROOT / base_cfg).resolve()
    if not base_cfg.is_file():
        print(f"Base config not found: {base_cfg}", file=sys.stderr)
        return 2

    base_text = base_cfg.read_text(encoding="utf-8")
    base_data = tomllib.loads(base_text)
    base_lr = (
        float(base_data.get("training", {}).get("learning_rate"))
        if isinstance(base_data.get("training", {}), dict)
        and base_data.get("training", {}).get("learning_rate") is not None
        else 1.0e-4
    )
    candidates = _parse_candidates(args.candidate_lrs)
    if base_lr not in candidates:
        candidates = [base_lr, *candidates]

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    stem = base_cfg.stem
    meta_out = (
        Path(args.metadata_out)
        if args.metadata_out
        else REPO_ROOT / "artifacts" / "lr_tuning" / f"{timestamp}_{stem}_metadata.json"
    )
    rec_out = (
        Path(args.recommended_config_out)
        if args.recommended_config_out
        else REPO_ROOT / "artifacts" / "lr_tuning" / f"{timestamp}_{stem}_recommended.toml"
    )
    if not meta_out.is_absolute():
        meta_out = (REPO_ROOT / meta_out).resolve()
    if not rec_out.is_absolute():
        rec_out = (REPO_ROOT / rec_out).resolve()

    trials: list[TrialResult] = []
    for lr in candidates:
        cfg_txt = _replace_training_scalar(base_text, "learning_rate", repr(float(lr)))
        cfg_txt = _replace_training_scalar(cfg_txt, "epochs", str(max(1, int(args.trial_epochs))))
        trial_cfg = REPO_ROOT / "artifacts" / "lr_tuning" / f"trial_{stem}_{lr:.8g}.toml"
        trial_cfg.parent.mkdir(parents=True, exist_ok=True)
        trial_cfg.write_text(cfg_txt, encoding="utf-8")

        rc, out, err, elapsed = _run_trial(trial_cfg, int(args.max_runtime_seconds))
        parsed = _extract_training_result(out)
        metric_name, metric_value = _metric_from_result(parsed)
        if metric_value is None:
            metric_name, metric_value = _metric_from_logs((out or "") + "\n" + (err or ""))
        epochs_completed = None
        if isinstance(parsed, dict):
            ec = parsed.get("epochs_run")
            if isinstance(ec, int):
                epochs_completed = ec
        timed_out = rc == 124
        trials.append(
            TrialResult(
                lr=float(lr),
                ok=metric_value is not None,
                elapsed_s=elapsed,
                metric_name=metric_name,
                metric_value=metric_value,
                epochs_completed=epochs_completed,
                exit_code=rc,
                timed_out=timed_out,
                stderr_tail=(err or "").splitlines()[-6:][0] if err else "",
            )
        )

    valid = [t for t in trials if t.ok and t.metric_value is not None]
    if not valid:
        print("No successful LR trials; see metadata for diagnostics.", file=sys.stderr)
        _write_metadata(
            meta_out,
            {
                "created_at_utc": timestamp,
                "base_config": str(base_cfg),
                "candidates": candidates,
                "trial_epochs": args.trial_epochs,
                "max_runtime_seconds": args.max_runtime_seconds,
                "trials": [asdict(t) for t in trials],
                "recommended_lr": base_lr,
                "promoted": False,
                "promotion_reason": "no successful candidates",
            },
        )
        return 1

    # Lower metric is better.
    best = min(valid, key=lambda t: float(t.metric_value))
    baseline = next((t for t in valid if abs(t.lr - base_lr) < 1e-18), None)
    promoted = False
    reason = "baseline retained"
    chosen = base_lr
    if baseline is None:
        promoted = True
        chosen = best.lr
        reason = "baseline trial unavailable; selected best valid candidate"
    else:
        if (
            best.lr != baseline.lr
            and float(best.metric_value) < float(baseline.metric_value)
            and float(best.elapsed_s) <= float(baseline.elapsed_s)
        ):
            promoted = True
            chosen = best.lr
            reason = "candidate beats baseline on metric with equal/lower runtime"
        else:
            promoted = False
            chosen = baseline.lr
            reason = "no candidate met promotion criteria vs baseline (metric + runtime)"

    tuned = _replace_training_scalar(base_text, "learning_rate", repr(float(chosen)))
    rec_out.parent.mkdir(parents=True, exist_ok=True)
    rec_out.write_text(tuned, encoding="utf-8")

    payload = {
        "created_at_utc": timestamp,
        "base_config": str(base_cfg),
        "metadata_path": str(meta_out),
        "recommended_config": str(rec_out),
        "trial_epochs": int(args.trial_epochs),
        "max_runtime_seconds": int(args.max_runtime_seconds),
        "candidates": [float(x) for x in candidates],
        "trials": [asdict(t) for t in trials],
        "baseline_lr": float(base_lr),
        "recommended_lr": float(chosen),
        "promoted": bool(promoted),
        "promotion_reason": reason,
        "reproducibility": {
            "seed": base_data.get("training", {}).get("seed"),
            "training_data_source": base_data.get("data", {}).get("source"),
            "data_path": base_data.get("data", {}).get("path"),
            "accelerator": base_data.get("hardware", {}).get("accelerator"),
            "metric_name": best.metric_name if best else "none",
        },
    }
    _write_metadata(meta_out, payload)

    if args.json:
        print(json.dumps(payload))
    else:
        print(f"recommended_lr={chosen}")
        print(f"promoted={promoted}")
        print(f"metadata={meta_out}")
        print(f"recommended_config={rec_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
