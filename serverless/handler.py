"""RunPod Serverless worker handler for qminiwasm training jobs.

Input contract (input dict):
  - qmw_action: "train"
  - config_rel: repo-relative TOML path (e.g. configs/training/wui_working.toml)
  - extra_env: optional ["KEY=value", ...]
  - toml_overlay: optional TOML fragment (non-secret) deep-merged into the base config before training
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from qminiwasm.engine.native_cli import qmw_grpc_train_argv, qmw_grpc_train_executable
from qminiwasm.runtime_modes import apply_optimized_auto_defaults

_MAX_OVERLAY_BYTES = 64 * 1024


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


def _loads_toml_bytes(raw: bytes) -> dict[str, Any]:
    text = raw.decode("utf-8")
    if sys.version_info >= (3, 11):
        import tomllib

        return tomllib.loads(text)
    import tomli  # type: ignore[import-untyped]

    return tomli.loads(text)


def _loads_toml_str(text: str) -> dict[str, Any]:
    return _loads_toml_bytes(text.encode("utf-8"))


def _deep_merge_dict(base: dict[str, Any], overlay: dict[str, Any]) -> dict[str, Any]:
    out = dict(base)
    for k, v in overlay.items():
        if k in out and isinstance(out[k], dict) and isinstance(v, dict):
            out[k] = _deep_merge_dict(out[k], v)
        else:
            out[k] = v
    return out


def _merge_training_config_to_temp(
    repo: Path,
    base_config: Path,
    overlay_toml: str,
) -> Path:
    base_doc = _loads_toml_bytes(base_config.read_bytes())
    over_doc = _loads_toml_str(overlay_toml)
    merged = _deep_merge_dict(base_doc, over_doc)
    from qminiwasm.engine.training_schema import TrainingConfig

    TrainingConfig.model_validate(merged)
    try:
        import tomli_w
    except ImportError as exc:
        raise RuntimeError(
            "tomli_w is required when input.toml_overlay is set; "
            "install training extras (e.g. pip install 'llm-pract[training]') including tomli-w."
        ) from exc
    out_dir = repo / "configs" / "training"
    out_dir.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(suffix=".toml", prefix=".wui_merged_", dir=str(out_dir))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(tomli_w.dumps(merged))
    except Exception:
        Path(tmp).unlink(missing_ok=True)
        raise
    return Path(tmp)


def _run_train(config_abs: Path) -> tuple[int, str, str]:
    repo = _repo_root()
    if qmw_grpc_train_executable() is None:
        return (
            127,
            "",
            "qmw-grpc-train not found on PATH. Install the Go binary (training-wui/cmd/qmw-grpc-train), "
            "set QMW_GRPC_TRAIN_BIN if it is not on PATH, start qminiwasm_training_engine_server, "
            "and set QMINIWASM_TRAINING_GRPC_ADDR if not using 127.0.0.1:50061. "
            "See docs/RUNPOD_SERVERLESS.md.",
        )
    cmd = qmw_grpc_train_argv(repo_root=repo, config_path=config_abs)
    proc = subprocess.run(
        cmd,
        cwd=str(repo),
        text=True,
        capture_output=True,
        env=os.environ.copy(),
    )
    return proc.returncode, proc.stdout, proc.stderr


def handle_training_job(
    input_payload: dict[str, Any],
    run_train: Any = _run_train,
) -> dict[str, Any]:
    apply_optimized_auto_defaults()
    if not isinstance(input_payload, dict):
        return {"ok": False, "error": "input must be an object"}

    action = _as_str(input_payload.get("qmw_action"))
    if action != "train":
        return {"ok": False, "error": f'unsupported qmw_action "{action}" (expected "train")'}

    config_rel = _as_str(input_payload.get("config_rel"))
    if not config_rel:
        return {"ok": False, "error": "missing input.config_rel"}

    _apply_extra_env(input_payload)
    repo = _repo_root()
    config_abs = (repo / config_rel).resolve()
    try:
        config_abs.relative_to(repo.resolve())
    except ValueError:
        return {"ok": False, "error": f"config_rel escapes repo root: {config_rel}"}
    if not config_abs.exists():
        return {"ok": False, "error": f"config not found: {config_abs}"}

    overlay_raw = input_payload.get("toml_overlay")
    train_path = config_abs
    merged_temp: Path | None = None
    if isinstance(overlay_raw, str) and overlay_raw.strip():
        if len(overlay_raw.encode("utf-8")) > _MAX_OVERLAY_BYTES:
            return {
                "ok": False,
                "error": f"toml_overlay exceeds {_MAX_OVERLAY_BYTES} bytes",
            }
        try:
            merged_temp = _merge_training_config_to_temp(repo, config_abs, overlay_raw)
            train_path = merged_temp
        except Exception as e:
            return {"ok": False, "error": f"toml_overlay merge failed: {e}"}

    try:
        code, out, err = run_train(train_path)
    finally:
        if merged_temp is not None:
            merged_temp.unlink(missing_ok=True)

    return {
        "ok": code == 0,
        "exit_code": code,
        "repo_root": str(repo.resolve()),
        "config_rel": config_rel,
        "stdout": out[-12000:],
        "stderr": err[-12000:],
    }


def handler(job: dict[str, Any]) -> dict[str, Any]:
    input_payload = job.get("input") if isinstance(job, dict) else {}
    return handle_training_job(input_payload)


def main() -> None:
    try:
        import runpod
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Missing dependency: runpod. Install with `pip install runpod` in the worker image."
        ) from exc
    runpod.serverless.start({"handler": handler})


if __name__ == "__main__":
    main()
