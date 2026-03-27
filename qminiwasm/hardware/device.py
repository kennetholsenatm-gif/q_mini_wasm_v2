"""Device selection for Intel XPU (Arc / Iris Xe), NVIDIA CUDA, and CPU.

This module provides a unified device dispatcher for AI training and inference.
It supports Intel **XPU** (discrete Arc or integrated **Iris Xe** when PyTorch XPU
/IPEX is installed), NVIDIA CUDA, and CPU. ``accelerator="sycl"`` is accepted as
an alias for XPU-first selection with CPU fallback. When ``accelerator`` is omitted,
defaults to CPU unless ``prefer_xpu=True`` is passed explicitly (no environment variables).
"""

from __future__ import annotations

import logging
import os
from typing import Literal, Tuple

import torch

logger = logging.getLogger(__name__)

# Optional: Intel Extension for PyTorch (IPEX) for older PyTorch or extra XPU features
_IPEX_AVAILABLE = False
try:
    import intel_extension_for_pytorch as ipex  # noqa: F401

    _IPEX_AVAILABLE = True
except ImportError:
    ipex = None
    pass

AcceleratorType = Literal["cuda", "xpu", "cpu", "sycl"]


def _env_flag(name: str, default: bool = False) -> bool:
    v = os.getenv(name, "").strip().lower()
    if not v:
        return default
    return v in {"1", "true", "yes", "on"}


def _cuda_available() -> bool:
    """Return True if CUDA is available."""
    return getattr(torch.cuda, "is_available", lambda: False)()


def _xpu_available() -> bool:
    """Return True if Intel XPU is available (Arc, Iris Xe, etc. via PyTorch XPU)."""
    xpu = getattr(torch, "xpu", None)
    if xpu is None:
        return False
    return getattr(xpu, "is_available", lambda: False)()


def _xpu_runtime_status() -> Tuple[bool, str]:
    """Return `(available, reason)` for XPU runtime availability diagnostics."""
    if getattr(torch, "xpu", None) is None:
        return (
            False,
            "this PyTorch build has no torch.xpu runtime; install an Intel XPU-enabled torch build",
        )
    if _xpu_available():
        return True, "torch.xpu.is_available() is true"
    if not _IPEX_AVAILABLE:
        return (
            False,
            "torch.xpu exists but reports unavailable and intel-extension-for-pytorch (IPEX) is not importable",
        )
    return False, "torch.xpu exists and IPEX imported, but torch.xpu.is_available() is false"


def _xpu_device_name(device_index: int = 0) -> str:
    """Best-effort XPU device name."""
    xpu = getattr(torch, "xpu", None)
    if xpu is None:
        return "unknown"
    getter = getattr(xpu, "get_device_name", None)
    if callable(getter):
        try:
            return str(getter(device_index))
        except Exception:
            return "unknown"
    return "unknown"


def get_xpu_backend_status(device_index: int | None = None) -> dict[str, object]:
    """Return a structured backend status for XPU capability gating and telemetry.

    `support_class` is one of:
    - `supported`: runtime available and likely production-capable
    - `experimental`: runtime available but known-risk hardware class (e.g. Iris Xe)
    - `unsupported`: runtime unavailable
    """
    idx = device_index if device_index is not None else 0
    available, reason = _xpu_runtime_status()
    name = _xpu_device_name(idx) if available else "unknown"
    lowered = name.lower()
    support_class = "unsupported"
    if available:
        if "iris" in lowered and "xe" in lowered:
            support_class = "experimental"
        else:
            support_class = "supported"
    return {
        "backend": "xpu",
        "available": bool(available),
        "support_class": support_class,
        "device_name": name,
        "reason": reason,
        "ipex_importable": bool(_IPEX_AVAILABLE),
        "strict_mode_env": "QMINIWASM_STRICT_XPU",
    }


def resolve_backend_policy(
    accelerator: AcceleratorType | None = None,
    device_index: int | None = None,
    *,
    prefer_xpu: bool | None = None,
) -> dict[str, object]:
    """Resolve accelerator request into a deterministic compat-first backend decision."""
    idx = device_index if device_index is not None else 0
    strict_xpu = _env_flag("QMINIWASM_STRICT_XPU", False)
    requested = accelerator if accelerator is not None else ("xpu" if prefer_xpu else "cpu")
    decision: dict[str, object] = {
        "requested_accelerator": requested,
        "selected_device": "cpu",
        "selected_backend": "cpu",
        "reason_code": "cpu_default",
        "reason": "using CPU default path",
    }

    if accelerator == "cuda":
        if _cuda_available():
            decision.update(
                {
                    "selected_device": f"cuda:{idx}",
                    "selected_backend": "cuda",
                    "reason_code": "cuda_selected",
                    "reason": "cuda available",
                }
            )
            return decision
        decision.update(
            {
                "reason_code": "cuda_unavailable_fallback_cpu",
                "reason": "CUDA requested but unavailable; using CPU",
            }
        )
        return decision

    if accelerator in ("xpu", "sycl"):
        status = get_xpu_backend_status(idx)
        if bool(status["available"]):
            decision.update(
                {
                    "selected_device": f"xpu:{idx}",
                    "selected_backend": "xpu",
                    "reason_code": (
                        "xpu_selected" if accelerator == "xpu" else "sycl_mapped_to_xpu_selected"
                    ),
                    "reason": str(status["reason"]),
                    "xpu_status": status,
                }
            )
            return decision
        if strict_xpu:
            raise RuntimeError(
                f"ACCELERATOR={accelerator} requested but unavailable under strict mode: {status['reason']}"
            )
        decision.update(
            {
                "reason_code": (
                    "xpu_unavailable_fallback_cpu"
                    if accelerator == "xpu"
                    else "sycl_unavailable_fallback_cpu"
                ),
                "reason": str(status["reason"]),
                "xpu_status": status,
            }
        )
        return decision

    if accelerator == "cpu":
        decision.update(
            {
                "selected_device": "cpu",
                "selected_backend": "cpu",
                "reason_code": "cpu_requested",
                "reason": "cpu explicitly requested",
            }
        )
        return decision

    if accelerator is None and prefer_xpu is True and _xpu_available():
        decision.update(
            {
                "selected_device": f"xpu:{idx}",
                "selected_backend": "xpu",
                "reason_code": "prefer_xpu_selected",
                "reason": "prefer_xpu enabled and xpu available",
            }
        )
        return decision
    return decision


def get_device(
    accelerator: AcceleratorType | None = None,
    device_index: int | None = None,
    *,
    prefer_xpu: bool | None = None,
) -> torch.device:
    """Return the best available device for training and inference.

    When ``accelerator`` is set in TOML / EngineConfig, that type is used (with
    fallback to CPU if unavailable). When ``accelerator`` is None, defaults to CPU
    unless ``prefer_xpu=True`` is passed explicitly by tests or callers.

    Args:
        accelerator: "cuda", "xpu", "cpu", or "sycl". If None, see ``prefer_xpu``.
        device_index: Device index for cuda or xpu (e.g. 0). Ignored on CPU.
        prefer_xpu: If True, try XPU when ``accelerator`` is None. If None or False,
            use CPU when ``accelerator`` is None.

    Returns:
        torch.device: cuda:index, xpu:index, or cpu.
    """
    idx = device_index if device_index is not None else 0

    if accelerator is not None:
        if accelerator not in ("cuda", "xpu", "cpu", "sycl"):
            raise ValueError(f"Unknown accelerator: {accelerator}")

    decision = resolve_backend_policy(
        accelerator=accelerator,
        device_index=device_index,
        prefer_xpu=prefer_xpu,
    )
    sel = str(decision.get("selected_device", "cpu"))
    if sel.startswith("cuda:"):
        dev = torch.device(sel)
        logger.info("Using CUDA device for training/inference: %s", dev)
        return dev
    if sel.startswith("xpu:"):
        dev = torch.device(sel)
        status = decision.get("xpu_status")
        if isinstance(status, dict):
            logger.info(
                "Using Intel XPU device for training/inference: %s (%s; support_class=%s)",
                dev,
                status.get("device_name"),
                status.get("support_class"),
            )
            if status.get("support_class") == "experimental":
                logger.warning(
                    "XPU selected on experimental hardware class (%s).",
                    status.get("device_name"),
                )
        else:
            logger.info("Using Intel XPU device for training/inference: %s", dev)
        return dev
    reason = str(decision.get("reason", "falling back to CPU"))
    if accelerator in ("xpu", "sycl"):
        logger.warning("ACCELERATOR=%s unresolved (%s); using CPU", accelerator, reason)
    else:
        logger.info("%s", reason)
    return torch.device("cpu")


def get_device_name(device: torch.device | None = None) -> str:
    """Return a human-readable device name for the given or current device.

    Args:
        device: If None, infer from get_device() with default args.

    Returns:
        e.g. "NVIDIA CUDA (cuda:0)", "Intel XPU (xpu:0)", or "CPU".
    """
    if device is None:
        device = get_device()
    if device.type == "cuda":
        name = getattr(torch.cuda, "get_device_name", lambda i: "NVIDIA GPU")(device.index or 0)
        return f"{name} (cuda:{device.index or 0})"
    if device.type == "xpu":
        return f"Intel XPU (xpu:{device.index or 0})"
    return "CPU"
