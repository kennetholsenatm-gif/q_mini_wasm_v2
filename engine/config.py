"""Engine config loaded from environment (WUI-injected or defaults)."""

from __future__ import annotations

import os
from typing import List, Optional


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
        epochs: int = 25,
        batch_size: int = 32,
        learning_rate: float = 1e-4,
        data_path: Optional[str] = None,
        training_data_source: Optional[str] = None,
        mesh_algorithms: Optional[str] = None,
        hf_dataset_config: Optional[str] = None,
        hf_num_samples: Optional[int] = None,
        hf_split: Optional[str] = None,
        hf_text_fields: Optional[List[str]] = None,
        seed: Optional[int] = None,
        grad_clip_norm: Optional[float] = None,
        lr_plateau_patience: Optional[int] = None,
        lr_plateau_factor: Optional[float] = None,
        lr_plateau_min_lr: Optional[float] = None,
        early_stop_patience: Optional[int] = None,
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
        self.training_data_source = (
            training_data_source
            or os.environ.get("TRAINING_DATA_SOURCE", "mesh")
        ).strip()
        _tds = self.training_data_source.lower()
        self.mesh_algorithms = mesh_algorithms or os.environ.get(
            "MESH_ALGORITHMS",
            "hash,encrypt,network,routing,consensus",
        )
        self.hf_dataset_config = hf_dataset_config or os.environ.get("HF_DATASET_CONFIG")
        self.hf_num_samples = hf_num_samples
        if self.hf_num_samples is None:
            _hns = os.environ.get("HF_NUM_SAMPLES", "").strip()
            if _hns.isdigit():
                self.hf_num_samples = int(_hns)
        self.hf_split = (hf_split or os.environ.get("HF_SPLIT") or "train").strip() or "train"
        self.hf_text_fields = hf_text_fields
        if self.hf_text_fields is None:
            _htf = os.environ.get("HF_TEXT_FIELDS", "").strip()
            if _htf:
                self.hf_text_fields = [x.strip() for x in _htf.split(",") if x.strip()]

        self.seed = seed
        if self.seed is None:
            _sd = os.environ.get("SEED", "").strip()
            if _sd:
                try:
                    self.seed = int(_sd)
                except ValueError:
                    self.seed = None

        self.grad_clip_norm = grad_clip_norm
        if self.grad_clip_norm is None:
            _gcn = os.environ.get("GRAD_CLIP_NORM", "").strip()
            if _gcn:
                try:
                    v = float(_gcn)
                    self.grad_clip_norm = v if v > 0 else None
                except ValueError:
                    self.grad_clip_norm = None

        self.lr_plateau_patience = lr_plateau_patience
        if self.lr_plateau_patience is None:
            _lp = os.environ.get("LR_PLATEAU_PATIENCE", "").strip()
            if _lp.isdigit():
                self.lr_plateau_patience = int(_lp)
            elif _lp == "0":
                self.lr_plateau_patience = 0
            elif _tds == "hf_tabular":
                # Identity-MSE on encoded code often plateaus; gentle decay helps without new data.
                self.lr_plateau_patience = 2
        self.lr_plateau_factor = (
            lr_plateau_factor
            if lr_plateau_factor is not None
            else float(os.environ.get("LR_PLATEAU_FACTOR", "0.5"))
        )
        self.lr_plateau_min_lr = (
            lr_plateau_min_lr
            if lr_plateau_min_lr is not None
            else float(os.environ.get("LR_PLATEAU_MIN_LR", "1e-7"))
        )

        self.early_stop_patience = early_stop_patience
        if self.early_stop_patience is None:
            _es = os.environ.get("EARLY_STOP_PATIENCE", "").strip()
            if _es.isdigit():
                self.early_stop_patience = int(_es)
