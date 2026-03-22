"""Reproducibility helpers for training runs."""

from __future__ import annotations

import logging
import random

_log = logging.getLogger(__name__)


def set_training_seed(seed: int) -> None:
    """Seed Python, NumPy (if installed), and PyTorch CPU/GPU/XPU."""
    random.seed(seed)

    try:
        import numpy as np

        np.random.seed(seed)
    except ImportError:
        _log.debug("NumPy not installed; skipping np.random.seed")

    import torch

    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    try:
        xm = getattr(torch, "xpu", None)
        if xm is not None and xm.is_available():
            xm.manual_seed(seed)
    except Exception:
        _log.debug("torch.xpu.manual_seed skipped or failed", exc_info=True)
