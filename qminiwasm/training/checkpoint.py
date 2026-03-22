"""Save/load trainable weights for ``QMiniWASM`` (quantum_router + ternary_expert + optional hybrid_adapter)."""

from __future__ import annotations

import inspect
import logging
from pathlib import Path
from typing import Any, Dict, Optional, Union

import torch
import torch.nn as nn

logger = logging.getLogger(__name__)

CHECKPOINT_FORMAT_VERSION = 1
D_MODEL = 4096


def build_checkpoint_payload(
    model: Any,
    meta: Optional[Dict[str, Any]] = None,
    cascade_policy: Optional[Union[nn.Module, Dict[str, Any]]] = None,
) -> Dict[str, Any]:
    """Assemble a checkpoint dict for ``torch.save``."""
    payload: Dict[str, Any] = {
        "format_version": CHECKPOINT_FORMAT_VERSION,
        "d_model": D_MODEL,
        "quantum_router": model.quantum_router.state_dict(),
        "ternary_expert": model.ternary_expert.state_dict(),
        "meta": dict(meta) if meta else {},
    }
    ha = getattr(model, "hybrid_adapter", None)
    if ha is not None:
        payload["hybrid_adapter"] = ha.state_dict()
    if cascade_policy is not None:
        if isinstance(cascade_policy, dict):
            payload["cascade_policy"] = cascade_policy
        else:
            payload["cascade_policy"] = cascade_policy.state_dict()
    return payload


def save_checkpoint(
    path: str | Path,
    model: Any,
    meta: Optional[Dict[str, Any]] = None,
    cascade_policy: Optional[nn.Module] = None,
) -> None:
    """Persist trainable submodules to a single file (creates parent directories)."""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    payload = build_checkpoint_payload(model, meta=meta, cascade_policy=cascade_policy)
    torch.save(payload, p)
    logger.info("Wrote checkpoint to %s", p)


def _torch_load_compat(path: Path, map_location: Any) -> Any:
    sig = None
    try:
        sig = inspect.signature(torch.load)
    except (TypeError, ValueError):
        pass
    if sig is not None and "weights_only" in sig.parameters:
        try:
            return torch.load(str(path), map_location=map_location, weights_only=True)
        except Exception:
            logger.debug("weights_only load failed; retrying full pickle.", exc_info=True)
    # Legacy checkpoints only; load from trusted paths (see pyproject bandit skip B614).
    return torch.load(str(path), map_location=map_location)


def load_checkpoint_into_model(
    model: Any,
    path: str | Path,
    map_location: Any = "cpu",
) -> Dict[str, Any]:
    """Load trainable submodule weights; return payload ``meta`` (may be empty)."""
    p = Path(path)
    if not p.is_file():
        raise FileNotFoundError(f"Checkpoint not found: {p}")

    payload = _torch_load_compat(p, map_location)
    if not isinstance(payload, dict):
        raise ValueError(f"Invalid checkpoint (expected dict): {p}")

    ver = payload.get("format_version", 0)
    if ver != CHECKPOINT_FORMAT_VERSION:
        logger.warning(
            "Checkpoint format_version=%s (expected %s); loading with best effort.",
            ver,
            CHECKPOINT_FORMAT_VERSION,
        )

    qr = payload.get("quantum_router")
    te = payload.get("ternary_expert")
    if qr is not None:
        inc = model.quantum_router.load_state_dict(qr, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "quantum_router load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )
    if te is not None:
        inc = model.ternary_expert.load_state_dict(te, strict=False)
        if inc.missing_keys or inc.unexpected_keys:
            logger.info(
                "ternary_expert load_state_dict: missing=%s unexpected=%s",
                inc.missing_keys,
                inc.unexpected_keys,
            )

    had = payload.get("hybrid_adapter")
    if isinstance(had, dict) and had:
        attach = getattr(model, "attach_hybrid_adapter_matching_state", None)
        if callable(attach):
            attach(had)
        mod = getattr(model, "hybrid_adapter", None)
        if mod is not None:
            inc = mod.load_state_dict(had, strict=False)
            if inc.missing_keys or inc.unexpected_keys:
                logger.info(
                    "hybrid_adapter load_state_dict: missing=%s unexpected=%s",
                    inc.missing_keys,
                    inc.unexpected_keys,
                )

    meta = payload.get("meta")
    return dict(meta) if isinstance(meta, dict) else {}


def load_cascade_policy_from_checkpoint(
    policy: nn.Module,
    path: str | Path,
    map_location: Any = "cpu",
) -> bool:
    """Load ``cascade_policy`` weights from checkpoint if present. Returns True if loaded."""
    p = Path(path)
    if not p.is_file():
        return False
    payload = _torch_load_compat(p, map_location)
    if not isinstance(payload, dict):
        return False
    cp = payload.get("cascade_policy")
    if not isinstance(cp, dict) or not cp:
        return False
    inc = policy.load_state_dict(cp, strict=False)
    if inc.missing_keys or inc.unexpected_keys:
        logger.info(
            "cascade_policy load_state_dict: missing=%s unexpected=%s",
            inc.missing_keys,
            inc.unexpected_keys,
        )
    return True
