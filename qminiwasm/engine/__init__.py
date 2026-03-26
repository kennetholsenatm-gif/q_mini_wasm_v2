"""ML training / serve process layer (TOML, env, FastAPI inference).

Run with ``python -m qminiwasm.engine`` (or ``python -m qminiwasm.cli train``).
"""

from .config import EngineConfig
from .train import main as train_main

__all__ = ["EngineConfig", "train_main"]
