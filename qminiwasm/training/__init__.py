"""Training curriculum and loop for Q-Mini-WASM."""

from __future__ import annotations

from typing import Any

from .cascade_rl import TinyCascadePolicy, ToyRoutingEnv, cascade_rl_train_step
from .distillation import MOPDLoss, MOPDLossConfig

__all__ = [
    "MOPDLoss",
    "MOPDLossConfig",
    "TinyCascadePolicy",
    "ToyRoutingEnv",
    "cascade_rl_train_step",
    "run_training_loop",
]


def __getattr__(name: str) -> Any:
    """Lazy import so ``qminiwasm.model`` can load ``cascade_rl`` without importing ``loop`` first."""
    if name == "run_training_loop":
        from .loop import run_training_loop

        return run_training_loop
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
