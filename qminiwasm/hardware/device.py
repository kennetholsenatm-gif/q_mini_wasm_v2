"""Device selection for Intel ARC (XPU) and CPU.

This module provides device selection that prefers Intel ARC / Intel XPU for AI training
instead of CUDA. It aligns with Intel's quantum and AI stack:
https://www.intel.com/content/www/us/en/research/quantum-computing.html

Use get_device() to obtain the best available device for tensors and model training.
"""

from __future__ import annotations

import logging
import os
from typing import Union

import torch

logger = logging.getLogger(__name__)

# Optional: Intel Extension for PyTorch (IPEX) for older PyTorch or extra XPU features
_IPEX_AVAILABLE = False
try:
    import intel_extension_for_pytorch  # noqa: F401
    _IPEX_AVAILABLE = True
except ImportError:
    pass


def _xpu_available() -> bool:
    """Return True if Intel XPU (e.g. Intel ARC) is available."""
    xpu = getattr(torch, "xpu", None)
    if xpu is None:
        return False
    return getattr(xpu, "is_available", lambda: False)()


def get_device(
    prefer_xpu: Union[bool, None] = None,
    device_index: Union[int, None] = None,
) -> torch.device:
    """Return the best available device for training and inference.

    Prefers Intel ARC (XPU) over CPU. CUDA is not used; this project targets
    Intel ARC for AI training per Intel quantum/AI stack.

    Args:
        prefer_xpu: If True, use Intel XPU when available; if False, use CPU. If None, use env PREFER_XPU (default 1).
        device_index: Optional XPU device index (e.g. 0 for first GPU). Ignored on CPU.

    Returns:
        torch.device: "xpu:index" if XPU is available and prefer_xpu, else "cpu".
    """
    if prefer_xpu is None:
        prefer_xpu = os.environ.get("PREFER_XPU", "1").strip().lower() in ("1", "true", "yes")
    if prefer_xpu and _xpu_available():
        idx = device_index if device_index is not None else 0
        dev = torch.device(f"xpu:{idx}")
        logger.info("Using Intel XPU (ARC) device for training/inference: %s", dev)
        return dev
    logger.info("Using CPU device (Intel XPU not available or not preferred)")
    return torch.device("cpu")


def get_device_name() -> str:
    """Return a human-readable device name (e.g. 'Intel ARC (XPU:0)' or 'CPU')."""
    if _xpu_available():
        return "Intel ARC (XPU)"
    return "CPU"
