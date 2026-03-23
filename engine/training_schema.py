"""Structured training configuration (TOML + Pydantic).

Secrets (e.g. Hugging Face tokens) stay in environment / .env — do not commit them in TOML.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, List, Optional

from pydantic import BaseModel, ConfigDict, Field

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib  # type: ignore[no-redef,import-untyped]


class HardwareSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    accelerator: Optional[str] = None
    device_index: Optional[int] = None
    quantum_backend: Optional[str] = None
    num_qubits: Optional[int] = None
    qaoa_layers: Optional[int] = None
    #: pennylane | qiskit_statevector | qiskit_ibm
    qaoa_execution_mode: Optional[str] = None
    ibm_qaoa_shots: Optional[int] = None
    #: hardware_only | prefer_hardware_fallback | simulator_only (also QUANTUM_EXECUTION_POLICY)
    quantum_execution_policy: Optional[str] = None
    diff_method: Optional[str] = None


class TrainingSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    epochs: Optional[int] = None
    batch_size: Optional[int] = None
    learning_rate: Optional[float] = None
    seed: Optional[int] = None
    grad_clip_norm: Optional[float] = None
    lr_plateau_patience: Optional[int] = None
    lr_plateau_factor: Optional[float] = None
    lr_plateau_min_lr: Optional[float] = None
    early_stop_patience: Optional[int] = None


class DataSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    source: Optional[str] = Field(
        None,
        description="TRAINING_DATA_SOURCE: mesh, corpus, hf_tabular",
    )
    path: Optional[str] = Field(None, description="DATA_PATH")
    mesh_algorithms: Optional[str] = None


class HuggingFaceSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    dataset_config: Optional[str] = None
    num_samples: Optional[int] = None
    split: Optional[str] = None
    text_fields: Optional[List[str]] = None
    context_fields: Optional[List[str]] = None
    wasi_slice_only: Optional[bool] = None
    wasi_max_scan: Optional[int] = None
    mesh_blend_fraction: Optional[float] = None


class CheckpointSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    load_path: Optional[str] = None
    save_path: Optional[str] = None
    best_path: Optional[str] = None
    latest_path: Optional[str] = None


class EvalSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    holdout_fraction: Optional[float] = None
    every_epoch: Optional[bool] = None
    target_mean_mse: Optional[float] = None
    stop_on_target_mse: Optional[bool] = None


class AdapterSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    hybrid_adapter: Optional[bool] = None
    hybrid_adapter_hidden: Optional[int] = None
    tequila_deadzone: Optional[float] = None
    lota_rank: Optional[int] = None
    use_tsign_ternary: Optional[bool] = None
    tsign_learning_rate: Optional[float] = None
    lota_merge_every_epoch: Optional[bool] = None


class CascadeSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    enabled: Optional[bool] = Field(None, description="CASCADE_RL")
    policy_lr: Optional[float] = None
    steps_per_epoch: Optional[int] = None
    group_size: Optional[int] = None
    state_dim: Optional[int] = None
    num_actions: Optional[int] = None
    mopd_lambda: Optional[float] = None
    mopd_feat_loss: Optional[str] = None
    seed_from_hidden: Optional[bool] = None
    use_router: Optional[bool] = None
    learned_projector: Optional[bool] = None
    router_hidden: Optional[int] = None
    couple_forward: Optional[bool] = None


class ServeSection(BaseModel):
    """Inference server (optional; used by configs/serve/*.toml)."""

    model_config = ConfigDict(extra="forbid")

    checkpoint: Optional[str] = Field(None, description="QMINIWASM_CHECKPOINT")
    hybrid_adapter: Optional[bool] = None
    hybrid_adapter_hidden: Optional[int] = None
    use_cascade_router: Optional[bool] = None
    cascade_state_dim: Optional[int] = None
    cascade_num_actions: Optional[int] = None
    cascade_router_hidden: Optional[int] = None


class TrainingConfig(BaseModel):
    """Root TOML document for training."""

    model_config = ConfigDict(extra="forbid")

    hardware: HardwareSection = Field(default_factory=HardwareSection)
    training: TrainingSection = Field(default_factory=TrainingSection)
    data: DataSection = Field(default_factory=DataSection)
    huggingface: HuggingFaceSection = Field(default_factory=HuggingFaceSection)
    checkpoint: CheckpointSection = Field(default_factory=CheckpointSection)
    eval: EvalSection = Field(default_factory=EvalSection)
    adapter: AdapterSection = Field(default_factory=AdapterSection)
    cascade: CascadeSection = Field(default_factory=CascadeSection)
    serve: ServeSection = Field(default_factory=ServeSection)

    def to_engine_kwargs(self) -> dict[str, Any]:
        """Map nested sections to EngineConfig keyword argument names (omit None)."""
        out: dict[str, Any] = {}
        h = self.hardware.model_dump(exclude_none=True)
        if "accelerator" in h:
            out["accelerator"] = h["accelerator"]
        if "device_index" in h:
            out["device_index"] = h["device_index"]
        if "quantum_backend" in h:
            out["quantum_backend"] = h["quantum_backend"]
        if "num_qubits" in h:
            out["num_qubits"] = h["num_qubits"]
        if "qaoa_layers" in h:
            out["qaoa_layers"] = h["qaoa_layers"]
        if "qaoa_execution_mode" in h:
            out["qaoa_execution_mode"] = h["qaoa_execution_mode"]
        if "ibm_qaoa_shots" in h:
            out["ibm_qaoa_shots"] = h["ibm_qaoa_shots"]
        if "quantum_execution_policy" in h:
            out["quantum_execution_policy"] = h["quantum_execution_policy"]
        if "diff_method" in h:
            out["diff_method"] = h["diff_method"]

        t = self.training.model_dump(exclude_none=True)
        for k, v in t.items():
            out[k] = v

        d = self.data.model_dump(exclude_none=True)
        if "source" in d:
            out["training_data_source"] = d["source"]
        if "path" in d:
            out["data_path"] = d["path"]
        if "mesh_algorithms" in d:
            out["mesh_algorithms"] = d["mesh_algorithms"]

        hf = self.huggingface.model_dump(exclude_none=True)
        key_map = {
            "dataset_config": "hf_dataset_config",
            "num_samples": "hf_num_samples",
            "split": "hf_split",
            "text_fields": "hf_text_fields",
            "context_fields": "hf_context_fields",
            "wasi_slice_only": "hf_wasi_slice_only",
            "wasi_max_scan": "hf_wasi_max_scan",
            "mesh_blend_fraction": "hf_mesh_blend_fraction",
        }
        for k, v in hf.items():
            if k in key_map:
                out[key_map[k]] = v
            else:
                out[k] = v

        ck = self.checkpoint.model_dump(exclude_none=True)
        ck_map = {
            "load_path": "checkpoint_load_path",
            "save_path": "checkpoint_save_path",
            "best_path": "checkpoint_best_path",
            "latest_path": "checkpoint_latest_path",
        }
        for k, v in ck.items():
            out[ck_map.get(k, k)] = v

        ev = self.eval.model_dump(exclude_none=True)
        ev_map = {
            "holdout_fraction": "eval_holdout_fraction",
            "every_epoch": "eval_every_epoch",
            "target_mean_mse": "target_mean_mse",
            "stop_on_target_mse": "stop_on_target_mse",
        }
        for k, v in ev.items():
            out[ev_map[k]] = v

        ad = self.adapter.model_dump(exclude_none=True)
        for k, v in ad.items():
            out[k] = v

        c = self.cascade.model_dump(exclude_none=True)
        c_map = {
            "enabled": "use_cascade_rl",
            "policy_lr": "cascade_policy_lr",
            "steps_per_epoch": "cascade_steps_per_epoch",
            "group_size": "cascade_group_size",
            "state_dim": "cascade_state_dim",
            "num_actions": "cascade_num_actions",
            "mopd_lambda": "cascade_mopd_lambda",
            "mopd_feat_loss": "cascade_mopd_feat_loss",
            "seed_from_hidden": "cascade_seed_from_hidden",
            "use_router": "use_cascade_router",
            "learned_projector": "cascade_learned_projector",
            "router_hidden": "cascade_router_hidden",
            "couple_forward": "cascade_couple_forward",
        }
        for k, v in c.items():
            out[c_map[k]] = v

        return out


def load_training_toml(path: str | Path) -> TrainingConfig:
    """Parse and validate a training TOML file."""
    p = Path(path)
    raw = p.read_bytes()
    data = tomllib.loads(raw.decode("utf-8"))
    return TrainingConfig.model_validate(data)


def load_serve_toml(path: str | Path) -> ServeSection:
    """Load only the [serve] table (file may contain training sections too)."""
    p = Path(path)
    data = tomllib.loads(p.read_bytes().decode("utf-8"))
    root = TrainingConfig.model_validate(data)
    return root.serve
