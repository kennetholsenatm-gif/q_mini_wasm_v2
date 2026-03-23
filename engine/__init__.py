"""Cloud instance ML engine entrypoint.

Run with: python -m engine
Loads device and quantum config from env and runs the training loop.
"""

from .config import EngineConfig
from .train import main as train_main

__all__ = ["EngineConfig", "train_main"]
