"""Training TOML, EngineConfig, and training loop helpers for tests and tooling.

The supported operator path is the Training WUI with the C++ gRPC engine; see
``training-wui/README.md`` and ``docs/TRAINING_NATIVE_PARITY.md``.
"""

from .config import EngineConfig
from .train import main as train_main

__all__ = ["EngineConfig", "train_main"]
