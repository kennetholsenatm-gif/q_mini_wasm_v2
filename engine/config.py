"""Engine config loaded from environment (WUI-injected or defaults)."""

from __future__ import annotations

import os
from typing import Optional


class EngineConfig:
    """Configuration for the ML engine on the cloud instance."""

    def __init__(
        self,
        accelerator: Optional[str] = None,
        device_index: Optional[int] = None,
        quantum_backend: Optional[str] = None,
        num_qubits: Optional[int] = None,
        qaoa_layers: Optional[int] = None,
        diff_method: Optional[str] = None,
        epochs: int = 10,
        batch_size: int = 32,
        learning_rate: float = 1e-4,
        data_path: Optional[str] = None,
    ):
        self.accelerator = accelerator or os.environ.get("ACCELERATOR", "").strip() or None
        _idx = os.environ.get("DEVICE_INDEX", "")
        self.device_index = (
            int(_idx) if _idx.isdigit() else (device_index if device_index is not None else 0)
        )
        self.quantum_backend = quantum_backend or os.environ.get("QUANTUM_BACKEND", "penny_lane")
        self.num_qubits = (
            num_qubits if num_qubits is not None else int(os.environ.get("NUM_QUBITS", "8"))
        )
        self.qaoa_layers = (
            qaoa_layers if qaoa_layers is not None else int(os.environ.get("QAOA_LAYERS", "3"))
        )
        self.diff_method = diff_method or os.environ.get("DIFF_METHOD", "parameter-shift")
        self.epochs = int(os.environ.get("EPOCHS", str(epochs)))
        self.batch_size = int(os.environ.get("BATCH_SIZE", str(batch_size)))
        self.learning_rate = float(os.environ.get("LEARNING_RATE", str(learning_rate)))
        self.data_path = data_path or os.environ.get("DATA_PATH")
