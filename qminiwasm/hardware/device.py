"""Device selection for Intel XPU (Arc / Iris Xe), NVIDIA CUDA, and CPU.

This module provides a unified device dispatcher for AI training and inference.
It supports Intel **XPU** (discrete Arc or integrated **Iris Xe** when PyTorch XPU
/IPEX is installed), NVIDIA CUDA, and CPU. ``accelerator="sycl"`` is accepted as
an alias for XPU-first selection with CPU fallback. When ``accelerator`` is omitted,
defaults to CPU unless ``prefer_xpu=True`` is passed explicitly (no environment variables).
"""

from __future__ import annotations

import logging
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
        if accelerator == "cuda":
            if _cuda_available():
                dev = torch.device(f"cuda:{idx}")
                logger.info("Using CUDA device for training/inference: %s", dev)
                return dev
            logger.info("CUDA requested but not available; using CPU")
            return torch.device("cpu")
        if accelerator == "xpu":
            ok, why = _xpu_runtime_status()
            if ok:
                dev = torch.device(f"xpu:{idx}")
                logger.info("Using Intel XPU device for training/inference: %s", dev)
                return dev
            logger.warning(
                "ACCELERATOR=xpu requested but XPU runtime is unavailable (%s); using CPU. "
                "Install an Intel XPU-enabled PyTorch build and intel-extension-for-pytorch (IPEX). "
                "SYCL/dpctl seeing Iris Xe only affects SYCLHardware helpers, not torch.nn training."
                " (IPEX importable=%s)",
                why,
                _IPEX_AVAILABLE,
            )
            return torch.device("cpu")
        if accelerator == "cpu":
            return torch.device("cpu")
        if accelerator == "sycl":
            if _xpu_available():
                dev = torch.device(f"xpu:{idx}")
                logger.info("Using SYCL accelerator via Intel XPU device: %s", dev)
                return dev
            logger.warning(
                "ACCELERATOR=sycl requested but no PyTorch XPU device is available; using CPU. "
                "This codebase currently maps SYCL to a single torch backend device (xpu) "
                "and does not schedule one training step across CPU+GPU simultaneously."
            )
            return torch.device("cpu")
        raise ValueError(f"Unknown accelerator: {accelerator}")

    if prefer_xpu is True and _xpu_available():
        dev = torch.device(f"xpu:{idx}")
        logger.info("Using Intel XPU device (prefer_xpu=True): %s", dev)
        return dev
    logger.info("Using CPU device (no accelerator in config)")
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
