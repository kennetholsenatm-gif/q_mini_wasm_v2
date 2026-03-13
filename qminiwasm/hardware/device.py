"""Device selection for Intel ARC (XPU), NVIDIA CUDA, and CPU.

This module provides a unified device dispatcher for AI training and inference.
It supports Intel ARC (XPU), NVIDIA CUDA, and CPU. When accelerator is not
specified, behavior is driven by env PREFER_XPU and PREFER_CUDA.
"""

from __future__ import annotations

import logging
import os
from typing import Literal, Union

import torch

logger = logging.getLogger(__name__)

# Optional: Intel Extension for PyTorch (IPEX) for older PyTorch or extra XPU features
_IPEX_AVAILABLE = False
try:
    import intel_extension_for_pytorch  # noqa: F401
    _IPEX_AVAILABLE = True
except ImportError:
    pass

AcceleratorType = Literal["cuda", "xpu", "cpu"]


def _cuda_available() -> bool:
    """Return True if CUDA is available."""
    return getattr(torch.cuda, "is_available", lambda: False)()


def _xpu_available() -> bool:
    """Return True if Intel XPU (e.g. Intel ARC) is available."""
    xpu = getattr(torch, "xpu", None)
    if xpu is None:
        return False
    return getattr(xpu, "is_available", lambda: False)()


def get_device(
    accelerator: AcceleratorType | None = None,
    device_index: int | None = None,
    *,
    prefer_xpu: bool | None = None,
) -> torch.device:
    """Return the best available device for training and inference.

    When accelerator is specified, that type is used (with fallback to CPU if
    unavailable). When accelerator is None, env PREFER_XPU and PREFER_CUDA
    are used; prefer_xpu (or legacy kwarg) overrides env for backward compatibility.

    Args:
        accelerator: "cuda", "xpu", or "cpu". If None, use env / prefer_xpu.
        device_index: Device index for cuda or xpu (e.g. 0). Ignored on CPU.
        prefer_xpu: Legacy: If True, use XPU; if False, prefer CPU (or CUDA if only that is set). If None, use env.

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
            if _xpu_available():
                dev = torch.device(f"xpu:{idx}")
                logger.info("Using Intel XPU (ARC) device for training/inference: %s", dev)
                return dev
            logger.info("XPU requested but not available; using CPU")
            return torch.device("cpu")
        if accelerator == "cpu":
            return torch.device("cpu")
        raise ValueError(f"Unknown accelerator: {accelerator}")

    # Legacy / env-driven: prefer_xpu takes precedence over env if explicitly set
    use_xpu = prefer_xpu
    if use_xpu is None:
        use_xpu = os.environ.get("PREFER_XPU", "1").strip().lower() in ("1", "true", "yes")
    use_cuda = os.environ.get("PREFER_CUDA", "0").strip().lower() in ("1", "true", "yes")
    if prefer_xpu is False:
        use_xpu = False

    if use_xpu and _xpu_available():
        dev = torch.device(f"xpu:{idx}")
        logger.info("Using Intel XPU (ARC) device for training/inference: %s", dev)
        return dev
    if use_cuda and _cuda_available():
        dev = torch.device(f"cuda:{idx}")
        logger.info("Using CUDA device for training/inference: %s", dev)
        return dev
    logger.info("Using CPU device")
    return torch.device("cpu")


def get_device_name(device: torch.device | None = None) -> str:
    """Return a human-readable device name for the given or current device.

    Args:
        device: If None, infer from get_device() with default args.

    Returns:
        e.g. "NVIDIA CUDA (cuda:0)", "Intel ARC (XPU)", or "CPU".
    """
    if device is None:
        device = get_device()
    if device.type == "cuda":
        name = getattr(torch.cuda, "get_device_name", lambda i: "NVIDIA GPU")(device.index or 0)
        return f"{name} (cuda:{device.index or 0})"
    if device.type == "xpu":
        return f"Intel ARC (XPU:{device.index or 0})"
    return "CPU"
