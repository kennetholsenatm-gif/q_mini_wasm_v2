"""Engine config: TOML file + explicit kwargs. Secrets (HF token) may be read from env."""

from __future__ import annotations

import logging
import os
from pathlib import Path
from typing import Any, Dict, List, Optional

from .secret_sanitize import sanitize_api_key_like


def _normalize_hf_extra_specs(items: Any) -> List[Dict[str, Any]]:
    """List of {path, dataset_config?} for additional HF datasets (max 8)."""
    if not items:
        return []
    out: List[Dict[str, Any]] = []
    if not isinstance(items, list):
        return []
    for it in items:
        if not isinstance(it, dict):
            continue
        path = str(it.get("path") or "").strip()
        if not path:
            continue
        dc = it.get("dataset_config")
        if dc is not None and str(dc).strip():
            out.append({"path": path, "dataset_config": str(dc).strip()})
        else:
            out.append({"path": path})
        if len(out) >= 8:
            break
    return out


class EngineConfig:
    """Configuration for the ML engine.

    Non-secret options must come from a training TOML file or explicit constructor
    arguments. The only environment variables read here are **HUGGING_FACE_HUB_TOKEN**
    and **HF_TOKEN** when ``hf_token`` is not passed explicitly.
    """

    def __init__(
        self,
        accelerator: Optional[str] = None,
        device_index: Optional[int] = None,
        quantum_backend: Optional[str] = None,
        num_qubits: Optional[int] = None,
        qaoa_layers: Optional[int] = None,
        qaoa_execution_mode: Optional[str] = None,
        ibm_qaoa_shots: Optional[int] = None,
        qaoa_simulator_backend: Optional[str] = None,
        qaoa_mps_max_bond_dim: Optional[int] = None,
        qaoa_prune_enabled: Optional[bool] = None,
        qaoa_prune_threshold: Optional[float] = None,
        qaoa_prune_min_nodes: Optional[int] = None,
        qaoa_warm_start_cache_ttl: Optional[int] = None,
        quantum_execution_policy: Optional[str] = None,
        diff_method: Optional[str] = None,
        epochs: Optional[int] = None,
        batch_size: Optional[int] = None,
        learning_rate: Optional[float] = None,
        data_path: Optional[str] = None,
        training_data_source: Optional[str] = None,
        mesh_algorithms: Optional[str] = None,
        hf_dataset_config: Optional[str] = None,
        hf_num_samples: Optional[int] = None,
        hf_split: Optional[str] = None,
        hf_text_fields: Optional[List[str]] = None,
        hf_context_fields: Optional[List[str]] = None,
        hf_wasi_slice_only: Optional[bool] = None,
        hf_wasi_max_scan: Optional[int] = None,
        hf_token: Optional[str] = None,
        seed: Optional[int] = None,
        grad_clip_norm: Optional[float] = None,
        lr_plateau_patience: Optional[int] = None,
        lr_plateau_factor: Optional[float] = None,
        lr_plateau_min_lr: Optional[float] = None,
        early_stop_patience: Optional[int] = None,
        checkpoint_load_path: Optional[str] = None,
        checkpoint_save_path: Optional[str] = None,
        checkpoint_best_path: Optional[str] = None,
        checkpoint_latest_path: Optional[str] = None,
        eval_holdout_fraction: Optional[float] = None,
        eval_every_epoch: Optional[bool] = None,
        eval_early_stop_patience: Optional[int] = None,
        target_mean_mse: Optional[float] = None,
        stop_on_target_mse: Optional[bool] = None,
        hybrid_adapter: Optional[bool] = None,
        hybrid_adapter_hidden: Optional[int] = None,
        tequila_deadzone: Optional[float] = None,
        lota_rank: Optional[int] = None,
        use_tsign_ternary: Optional[bool] = None,
        tsign_learning_rate: Optional[float] = None,
        lota_merge_every_epoch: Optional[bool] = None,
        use_cascade_rl: Optional[bool] = None,
        cascade_policy_lr: Optional[float] = None,
        cascade_steps_per_epoch: Optional[int] = None,
        cascade_group_size: Optional[int] = None,
        cascade_state_dim: Optional[int] = None,
        cascade_num_actions: Optional[int] = None,
        cascade_mopd_lambda: Optional[float] = None,
        cascade_mopd_feat_loss: Optional[str] = None,
        cascade_seed_from_hidden: Optional[bool] = None,
        use_cascade_router: Optional[bool] = None,
        cascade_learned_projector: Optional[bool] = None,
        cascade_router_hidden: Optional[int] = None,
        cascade_couple_forward: Optional[bool] = None,
        hf_mesh_blend_fraction: Optional[float] = None,
        hf_extra_specs: Optional[List[Dict[str, Any]]] = None,
        hf_streaming: Optional[bool] = None,
        hf_max_scan_rows: Optional[int] = None,
        hf_max_buffered_rows: Optional[int] = None,
        hf_text_truncate_bytes: Optional[int] = None,
        hf_deterministic_keep_every_n: Optional[int] = None,
        hf_dataset_revision: Optional[str] = None,
        wasm_store_memory_limit_mb: Optional[int] = None,
        wasm_fallback_policy: Optional[str] = None,
        wasm_force_mock: Optional[bool] = None,
        wasm_store_instance_limit: Optional[int] = None,
        wasm_store_memories_limit: Optional[int] = None,
    ):
        self.accelerator = accelerator
        self.device_index = int(device_index) if device_index is not None else 0
        self.quantum_backend = quantum_backend if quantum_backend is not None else "penny_lane"
        self.num_qubits = int(num_qubits) if num_qubits is not None else 8
        self.qaoa_layers = int(qaoa_layers) if qaoa_layers is not None else 3
        _qem = qaoa_execution_mode if qaoa_execution_mode is not None else "pennylane"
        self.qaoa_execution_mode = (_qem or "pennylane").strip().lower()
        self.ibm_qaoa_shots = int(ibm_qaoa_shots) if ibm_qaoa_shots is not None else 1024
        self.qaoa_simulator_backend = (
            str(qaoa_simulator_backend).strip().lower()
            if qaoa_simulator_backend is not None
            else "auto"
        )
        if qaoa_mps_max_bond_dim is not None:
            self.qaoa_mps_max_bond_dim = int(qaoa_mps_max_bond_dim)
        else:
            self.qaoa_mps_max_bond_dim = None
        if qaoa_prune_enabled is not None:
            self.qaoa_prune_enabled = bool(qaoa_prune_enabled)
        else:
            self.qaoa_prune_enabled = False
        if qaoa_prune_threshold is not None:
            self.qaoa_prune_threshold = float(qaoa_prune_threshold)
        else:
            self.qaoa_prune_threshold = 0.0
        if qaoa_prune_min_nodes is not None:
            self.qaoa_prune_min_nodes = max(1, int(qaoa_prune_min_nodes))
        else:
            self.qaoa_prune_min_nodes = 4
        if qaoa_warm_start_cache_ttl is not None:
            self.qaoa_warm_start_cache_ttl = max(1, int(qaoa_warm_start_cache_ttl))
        else:
            self.qaoa_warm_start_cache_ttl = 128
        self.quantum_execution_policy = (quantum_execution_policy or "").strip().lower()
        if (
            self.quantum_execution_policy == "hardware_only"
            and self.qaoa_execution_mode == "pennylane"
        ):
            _log = logging.getLogger(__name__)
            _log.info(
                "quantum_execution_policy=hardware_only: using qaoa_execution_mode=qiskit_ibm "
                "(pennylane would skip real QAOA in HybridQuantumMoE)"
            )
            self.qaoa_execution_mode = "qiskit_ibm"
        self.diff_method = diff_method if diff_method is not None else "parameter-shift"
        self.epochs = int(epochs) if epochs is not None else 25
        self.batch_size = int(batch_size) if batch_size is not None else 32
        if learning_rate is not None:
            self.learning_rate = float(learning_rate)
        else:
            self.learning_rate = 1e-4

        self.data_path = data_path
        if training_data_source is not None:
            self.training_data_source = str(training_data_source).strip() or "mesh"
        else:
            self.training_data_source = "mesh"

        _tds = self.training_data_source.lower()
        if learning_rate is None and _tds == "hf_tabular" and self.learning_rate == 1e-4:
            self.learning_rate = 1.5e-4

        self.mesh_algorithms = (
            mesh_algorithms
            if mesh_algorithms is not None
            else "hash,encrypt,network,routing,consensus"
        )
        self.hf_dataset_config = hf_dataset_config
        if hf_num_samples is not None:
            self.hf_num_samples = hf_num_samples
        else:
            self.hf_num_samples = None

        if hf_split is not None:
            self.hf_split = str(hf_split).strip() or "train"
        else:
            self.hf_split = "train"

        if hf_text_fields is not None:
            self.hf_text_fields = hf_text_fields
        else:
            self.hf_text_fields = None

        if hf_context_fields is not None:
            self.hf_context_fields = hf_context_fields
        else:
            self.hf_context_fields = None

        if hf_wasi_slice_only is not None:
            self.hf_wasi_slice_only = hf_wasi_slice_only
        else:
            self.hf_wasi_slice_only = False

        if hf_wasi_max_scan is not None:
            self.hf_wasi_max_scan = hf_wasi_max_scan
        else:
            self.hf_wasi_max_scan = None

        self.hf_token = hf_token
        if self.hf_token is None:
            _hub = os.environ.get("HUGGING_FACE_HUB_TOKEN", "").strip()
            _hf = os.environ.get("HF_TOKEN", "").strip()
            self.hf_token = _hub or _hf or None
        self.hf_token = sanitize_api_key_like(self.hf_token)

        self.seed = seed if seed is not None else None

        if grad_clip_norm is not None:
            self.grad_clip_norm = grad_clip_norm if grad_clip_norm > 0 else None
        elif _tds == "hf_tabular":
            self.grad_clip_norm = 1.0
        else:
            self.grad_clip_norm = None

        if lr_plateau_patience is not None:
            self.lr_plateau_patience = lr_plateau_patience
        elif _tds == "hf_tabular":
            self.lr_plateau_patience = 2
        else:
            self.lr_plateau_patience = None

        self.lr_plateau_factor = float(lr_plateau_factor) if lr_plateau_factor is not None else 0.5
        self.lr_plateau_min_lr = float(lr_plateau_min_lr) if lr_plateau_min_lr is not None else 1e-7

        if early_stop_patience is not None:
            self.early_stop_patience = early_stop_patience
        else:
            self.early_stop_patience = None

        self.checkpoint_load_path = checkpoint_load_path
        self.checkpoint_save_path = checkpoint_save_path
        self.checkpoint_best_path = checkpoint_best_path
        self.checkpoint_latest_path = checkpoint_latest_path

        if eval_holdout_fraction is not None:
            self.eval_holdout_fraction = float(eval_holdout_fraction)
        else:
            self.eval_holdout_fraction = 0.0

        if eval_every_epoch is not None:
            self.eval_every_epoch = eval_every_epoch
        else:
            self.eval_every_epoch = False

        if eval_early_stop_patience is not None and eval_early_stop_patience > 0:
            self.eval_early_stop_patience = int(eval_early_stop_patience)
        else:
            self.eval_early_stop_patience = None

        if target_mean_mse is not None:
            self.target_mean_mse = target_mean_mse
        elif _tds == "hf_tabular":
            self.target_mean_mse = 1e-4
        else:
            self.target_mean_mse = None

        if stop_on_target_mse is not None:
            self.stop_on_target_mse = stop_on_target_mse
        else:
            self.stop_on_target_mse = False

        if hybrid_adapter is not None:
            self.hybrid_adapter = hybrid_adapter
        else:
            self.hybrid_adapter = False

        if hybrid_adapter_hidden is not None:
            self.hybrid_adapter_hidden = max(32, int(hybrid_adapter_hidden))
        else:
            self.hybrid_adapter_hidden = 1024

        if tequila_deadzone is not None:
            self.tequila_deadzone = float(tequila_deadzone)
        else:
            self.tequila_deadzone = 0.0

        if lota_rank is not None:
            self.lota_rank = int(lota_rank)
        else:
            self.lota_rank = 0

        if use_tsign_ternary is not None:
            self.use_tsign_ternary = use_tsign_ternary
        else:
            self.use_tsign_ternary = False

        if tsign_learning_rate is not None:
            self.tsign_learning_rate = float(tsign_learning_rate)
        else:
            self.tsign_learning_rate = 1e-3

        if lota_merge_every_epoch is not None:
            self.lota_merge_every_epoch = lota_merge_every_epoch
        else:
            self.lota_merge_every_epoch = False

        if use_cascade_rl is not None:
            self.use_cascade_rl = use_cascade_rl
        else:
            self.use_cascade_rl = True

        if cascade_policy_lr is not None:
            self.cascade_policy_lr = float(cascade_policy_lr)
        elif _tds == "hf_tabular":
            self.cascade_policy_lr = 0.5 * float(self.learning_rate)
        else:
            self.cascade_policy_lr = self.learning_rate

        if cascade_steps_per_epoch is not None:
            self.cascade_steps_per_epoch = int(cascade_steps_per_epoch)
        else:
            self.cascade_steps_per_epoch = 2

        if cascade_group_size is not None:
            self.cascade_group_size = int(cascade_group_size)
        else:
            self.cascade_group_size = 4

        if cascade_state_dim is not None:
            self.cascade_state_dim = int(cascade_state_dim)
        else:
            self.cascade_state_dim = 8

        if cascade_num_actions is not None:
            self.cascade_num_actions = int(cascade_num_actions)
        else:
            self.cascade_num_actions = 4

        if cascade_mopd_lambda is not None:
            self.cascade_mopd_lambda = float(cascade_mopd_lambda)
        else:
            self.cascade_mopd_lambda = 0.0

        if cascade_mopd_feat_loss is not None:
            self.cascade_mopd_feat_loss = cascade_mopd_feat_loss
        else:
            self.cascade_mopd_feat_loss = "mse"

        if cascade_seed_from_hidden is not None:
            self.cascade_seed_from_hidden = cascade_seed_from_hidden
        else:
            self.cascade_seed_from_hidden = True

        if use_cascade_router is not None:
            self.use_cascade_router = use_cascade_router
        else:
            self.use_cascade_router = False

        if cascade_learned_projector is not None:
            self.cascade_learned_projector = cascade_learned_projector
        else:
            self.cascade_learned_projector = False

        if cascade_router_hidden is not None:
            self.cascade_router_hidden = int(cascade_router_hidden)
        else:
            self.cascade_router_hidden = 32

        if cascade_couple_forward is not None:
            self.cascade_couple_forward = cascade_couple_forward
        else:
            self.cascade_couple_forward = True

        if hf_mesh_blend_fraction is not None:
            self.hf_mesh_blend_fraction = max(0.0, float(hf_mesh_blend_fraction))
        else:
            self.hf_mesh_blend_fraction = 0.0

        if hf_extra_specs is not None:
            self.hf_extra_specs = _normalize_hf_extra_specs(hf_extra_specs)
        else:
            self.hf_extra_specs = []

        if hf_streaming is not None:
            self.hf_streaming = bool(hf_streaming)
        else:
            self.hf_streaming = False

        if hf_max_scan_rows is not None:
            self.hf_max_scan_rows = int(hf_max_scan_rows)
        else:
            self.hf_max_scan_rows = None

        if hf_max_buffered_rows is not None:
            self.hf_max_buffered_rows = int(hf_max_buffered_rows)
        else:
            self.hf_max_buffered_rows = None

        if hf_text_truncate_bytes is not None:
            self.hf_text_truncate_bytes = int(hf_text_truncate_bytes)
        else:
            self.hf_text_truncate_bytes = None

        if hf_deterministic_keep_every_n is not None:
            self.hf_deterministic_keep_every_n = int(hf_deterministic_keep_every_n)
        else:
            self.hf_deterministic_keep_every_n = None

        if hf_dataset_revision is not None:
            self.hf_dataset_revision = str(hf_dataset_revision).strip() or "main"
        else:
            self.hf_dataset_revision = "main"

        self.wasm_store_memory_limit_mb = wasm_store_memory_limit_mb
        self.wasm_fallback_policy = wasm_fallback_policy
        self.wasm_force_mock = wasm_force_mock
        self.wasm_store_instance_limit = wasm_store_instance_limit
        self.wasm_store_memories_limit = wasm_store_memories_limit

    def wasm_runtime_kwargs(self) -> Dict[str, Any]:
        """Build kwargs for :class:`qminiwasm.wasm_host.engine.WasmRuntimeConfig`."""
        from qminiwasm.wasm_host.engine import (
            DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES,
            WasmRuntimeConfig,
        )

        mb = self.wasm_store_memory_limit_mb
        lim_b = (
            int(mb) * 1024 * 1024
            if mb is not None and mb > 0
            else DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES
        )
        # Override TOML/default without editing config (e.g. Docker image still on old wheel).
        env_bytes = os.environ.get("QMW_WASM_STORE_MEMORY_LIMIT_BYTES", "").strip()
        if env_bytes:
            try:
                lim_b = max(1, int(env_bytes, 10))
            except ValueError:
                pass
        else:
            env_mb = os.environ.get("QMW_WASM_STORE_MEMORY_LIMIT_MB", "").strip()
            if env_mb:
                try:
                    lim_b = max(1, int(env_mb, 10)) * 1024 * 1024
                except ValueError:
                    pass
        fp = (self.wasm_fallback_policy or "mock").strip().lower()
        if fp in ("error", "fail", "strict"):
            fp = "error"
        else:
            fp = "mock"
        fm = bool(self.wasm_force_mock) if self.wasm_force_mock is not None else False
        return {
            "runtime": WasmRuntimeConfig(
                store_memory_limit_bytes=lim_b,
                fallback_policy=fp,
                force_mock=fm,
                store_instance_limit=self.wasm_store_instance_limit,
                store_memories_limit=self.wasm_store_memories_limit,
            )
        }

    @classmethod
    def from_training_toml(cls, path: str | Path) -> EngineConfig:
        """Load non-secret options from a TOML file; HF token still comes from env if unset."""
        from .training_schema import load_training_toml

        file_cfg = load_training_toml(path)
        kwargs = file_cfg.to_engine_kwargs()
        inst = cls(**kwargs)
        _raise_if_hf_multi_without_specs(inst, path)
        return inst


def _raise_if_hf_multi_without_specs(cfg: EngineConfig, path: str | Path) -> None:
    """Fail fast when extras-only HF mode has no datasets."""
    tds = (cfg.training_data_source or "mesh").strip().lower()
    if tds != "hf_tabular":
        return
    dp = (cfg.data_path or "").strip().lower()
    if dp not in ("qminiwasm/hf-multi", "qminiwasm/multi"):
        return
    if cfg.hf_extra_specs:
        return
    p = Path(path).resolve()
    raise ValueError(
        f"{p}: [data].path is qminiwasm/hf-multi (extras-only Hub mode) but no datasets were "
        "configured. Add [huggingface].extra_specs in this TOML (e.g. from Dataset Builder "
        "Apply mix), e.g. "
        '[{path = "code-search-net/code_search_net", dataset_config = "python"}].'
    )


def load_training_config(path: str | Path) -> Any:
    """Load and validate training TOML (Pydantic root model)."""
    from .training_schema import load_training_toml

    return load_training_toml(path)
