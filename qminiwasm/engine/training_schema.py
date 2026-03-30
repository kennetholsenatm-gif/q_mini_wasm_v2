# mypy: ignore-errors
"""Structured training configuration (TOML + Pydantic).

Secrets (e.g. Hugging Face tokens) stay in environment / .env — do not commit them in TOML.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, List, Literal, Optional

from pydantic import BaseModel, ConfigDict, Field, field_validator

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
    qaoa_simulator_backend: Optional[str] = None
    qaoa_mps_max_bond_dim: Optional[int] = None
    qaoa_prune_enabled: Optional[bool] = None
    qaoa_prune_threshold: Optional[float] = None
    qaoa_prune_min_nodes: Optional[int] = None
    qaoa_warm_start_cache_ttl: Optional[int] = None
    #: hardware_only | prefer_hardware_fallback | simulator_only (also QUANTUM_EXECUTION_POLICY)
    quantum_execution_policy: Optional[str] = None
    diff_method: Optional[str] = None


class TrainingSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    epochs: Optional[int] = None
    batch_size: Optional[int] = None
    #: Host-side DataLoader workers for supervised batch collation (TOML / WUI only).
    dataloader_num_workers: Optional[int] = None
    learning_rate: Optional[float] = None
    seed: Optional[int] = None
    grad_clip_norm: Optional[float] = None
    lr_plateau_patience: Optional[int] = None
    lr_plateau_factor: Optional[float] = None
    lr_plateau_min_lr: Optional[float] = None
    early_stop_patience: Optional[int] = None
    #: Structured ``qmw_xpu_mem`` stdout lines when training on XPU (WUI / TOML; no env required).
    log_xpu_memory: Optional[bool] = None
    #: Reset ``torch.xpu`` peak memory stats each epoch (use with ``log_xpu_memory``).
    log_xpu_memory_reset_peak: Optional[bool] = None
    #: ``qmw_train_throughput`` line after each supervised phase (wall time, samples/s, host RSS).
    log_train_throughput: Optional[bool] = None
    #: On XPU/SYCL when True: grad clip 1.0 and LR plateau patience 2 if TOML omits them.
    auto_stabilize: Optional[bool] = None


class DataSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    source: Optional[str] = Field(
        None,
        description="TRAINING_DATA_SOURCE: mesh, corpus, hf_tabular",
    )
    path: Optional[str] = Field(None, description="DATA_PATH")
    mesh_algorithms: Optional[str] = None


class HFExtraSpec(BaseModel):
    """Additional HF dataset (+ optional config) mixed with ``data.path`` / ``dataset_config``."""

    model_config = ConfigDict(extra="forbid")

    path: str = Field(..., description="HF dataset id (same format as [data].path)")
    dataset_config: Optional[str] = None


class HuggingFaceSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    dataset_config: Optional[str] = None
    #: Hub git ref (branch, tag, or commit) passed to ``datasets.load_dataset(..., revision=)``.
    dataset_revision: Optional[str] = None
    num_samples: Optional[int] = None
    split: Optional[str] = None
    text_fields: Optional[List[str]] = None
    context_fields: Optional[List[str]] = None
    wasi_slice_only: Optional[bool] = None
    wasi_max_scan: Optional[int] = None
    mesh_blend_fraction: Optional[float] = None
    #: Extra Hub datasets (deduped, unbounded); merged with primary ``[data].path`` when applicable.
    extra_specs: Optional[List[HFExtraSpec]] = None
    streaming: Optional[bool] = None
    max_scan_rows: Optional[int] = None
    max_buffered_rows: Optional[int] = None
    text_truncate_bytes: Optional[int] = None
    deterministic_keep_every_n: Optional[int] = None


class CheckpointSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    load_path: Optional[str] = None
    save_path: Optional[str] = None
    best_path: Optional[str] = None
    latest_path: Optional[str] = None


class TpemSection(BaseModel):
    """Trainable TPEM artifact paths (load/save/best/latest).

    When the legacy persistence table and ``[tpem]`` both set a field, **``[tpem]``
    wins** for that field.
    """

    model_config = ConfigDict(extra="forbid")

    load_path: Optional[str] = None
    save_path: Optional[str] = None
    best_path: Optional[str] = None
    latest_path: Optional[str] = None


class EvalSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    holdout_fraction: Optional[float] = None
    every_epoch: Optional[bool] = None
    #: Holdout eval plateau: stop after N epochs without eval MSE improvement.
    early_stop_patience: Optional[int] = None
    target_mean_mse: Optional[float] = None
    stop_on_target_mse: Optional[bool] = None


class ModelSection(BaseModel):
    """QMiniWASM geometry: internal width/depth and WASM/HF I/O width (memory_encode)."""

    model_config = ConfigDict(extra="forbid")

    #: Internal hidden width (ternary blocks, adapter, tropical attention).
    d_model: Optional[int] = Field(None, ge=32, le=1_048_576)
    #: Stacked residual ternary experts (each d_model→d_model).
    num_ternary_blocks: Optional[int] = Field(None, ge=1, le=1024)
    #: Mesh/HF encoder width (default 4096). When != ``d_model``, stem/head Linears map I/O.
    io_d_model: Optional[int] = Field(None, ge=8, le=1_048_576)
    #: When True, apply one TropicalAttention after the ternary stack inside hybrid_inference.
    tropical_attn_per_block: Optional[bool] = None
    #: When ``tropical_attn_per_block`` is True: ``tropical`` (max-plus hull) or ``bloch`` (fidelity).
    attention_backend: Optional[Literal["tropical", "bloch"]] = None


class DistributedSection(BaseModel):
    """Optional large-model training toggles (Python loop)."""

    model_config = ConfigDict(extra="forbid")

    gradient_checkpointing: Optional[bool] = None
    gradient_accumulation_steps: Optional[int] = Field(None, ge=1)
    #: torch.autocast float16/bfloat16 on CUDA/XPU when True (CPU: no-op).
    amp: Optional[bool] = None
    #: Wrap model in FSDP when torch.distributed is initialized (multi-process).
    fsdp: Optional[bool] = None
    #: Wrap model in DDP when torch.distributed is initialized.
    ddp: Optional[bool] = None


class AdapterSection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    hybrid_adapter: Optional[bool] = None
    hybrid_adapter_hidden: Optional[int] = None
    tequila_deadzone: Optional[float] = None
    lota_rank: Optional[int] = None
    use_tsign_ternary: Optional[bool] = None
    tsign_learning_rate: Optional[float] = None
    lota_merge_every_epoch: Optional[bool] = None


class CascadeCurriculumLoopSection(BaseModel):
    """Native C++ automated cascade curriculum (WUI maps this to gRPC; Python engine ignores)."""

    model_config = ConfigDict(extra="forbid")

    enabled: Optional[bool] = None
    max_heal_rounds: Optional[int] = None
    teacher_checkpoint_path: Optional[str] = None
    ptqtp_num_planes: Optional[int] = None
    gate_target_val_mse: Optional[float] = None
    gate_max_tpem_mib: Optional[float] = None
    run_taxonomy_linter: Optional[bool] = None
    heal_learning_rate_scale: Optional[float] = None
    teacher_epoch_fraction: Optional[float] = None
    heal_epochs_per_round: Optional[int] = None
    max_curriculum_cycles: Optional[int] = None


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
    #: Default cascade policy loss: ``grpo`` or ``cispo`` (per-phase override in ``training_phases``).
    policy_optimizer: Optional[Literal["grpo", "cispo"]] = None
    #: CISPO trust-region half-width on the importance ratio (symmetric clip).
    cispo_clip_epsilon: Optional[float] = Field(None, ge=0.0, le=1.0)


class TrainingPhaseSection(BaseModel):
    """One row of ``[[training_phases]]`` (Unified Training Matrix)."""

    model_config = ConfigDict(extra="forbid")

    name: str = Field(..., min_length=1)
    epochs: int = Field(..., ge=1)
    supervised: bool = True
    cascade_rl: bool = True
    freeze_model_backbone: bool = False
    router_only: bool = False
    cascade_policy_optimizer: Literal["grpo", "cispo"] = "grpo"
    cispo_clip_epsilon: Optional[float] = Field(None, ge=0.0, le=1.0)
    cascade_mopd_lambda: Optional[float] = Field(None, ge=0.0)
    tequila_deadzone: Optional[float] = Field(None, ge=0.0, le=1.0)
    freeze_ternary_experts: bool = False


class WasmSection(BaseModel):
    """Wasmtime limits; see ``WasmRuntimeConfig`` in ``qminiwasm.wasm_host.engine``."""

    model_config = ConfigDict(extra="forbid")

    store_memory_limit_mb: Optional[int] = None
    fallback_policy: Optional[str] = Field(
        None, description="mock: fall back to mock WASM on init/exec failure; error: raise"
    )
    backend: Optional[str] = Field(
        None,
        description=(
            "wasmtime (default), wasmedge_native (experimental native bridge), "
            "or enclave_adapter (feature-flagged Enclave-as-Code dispatch seam)"
        ),
    )
    force_mock: Optional[bool] = None
    store_instance_limit: Optional[int] = None
    store_memories_limit: Optional[int] = None


class ServeSection(BaseModel):
    """Inference server (optional; used by configs/serve/*.toml)."""

    model_config = ConfigDict(extra="forbid")

    #: Optional overrides when checkpoint omits geometry (prefer checkpoint meta when present).
    d_model: Optional[int] = Field(None, ge=32, le=1_048_576)
    num_ternary_blocks: Optional[int] = Field(None, ge=1, le=1024)
    io_d_model: Optional[int] = Field(None, ge=8, le=1_048_576)

    checkpoint: Optional[str] = Field(None, description="QMINIWASM_CHECKPOINT (legacy TOML key)")
    tpem: Optional[str] = Field(
        None,
        description=(
            "Trainable TPEM .pt path; preferred over the legacy weight path when " "both are set"
        ),
    )
    hybrid_adapter: Optional[bool] = None
    hybrid_adapter_hidden: Optional[int] = None
    use_cascade_router: Optional[bool] = None
    cascade_state_dim: Optional[int] = None
    cascade_num_actions: Optional[int] = None
    cascade_router_hidden: Optional[int] = None


class EnclaveSection(BaseModel):
    """WASM TPEM / EF tiering and CGE threshold (``[enclave]`` in serve TOML).

    Runtime policy uses preset+override precedence:
    explicit overrides > tier presets > runtime defaults.
    """

    model_config = ConfigDict(extra="forbid")

    enclave_footprint_mb: Optional[float] = Field(
        None,
        ge=0.0,
        description="Static Enclave Footprint (EF): TPEM + heap budget in MB",
    )
    enclave_tier: Optional[Literal["micro", "meso", "macro", "workgroup", "enterprise_core"]] = None
    certainty_scalar_threshold: Optional[float] = Field(
        None,
        ge=0.0,
        le=1.0,
        description="Halts ECL locally when certainty >= threshold; mirrors T_conf / CGE policy",
    )
    max_linear_memory_pages: Optional[int] = Field(
        None,
        ge=1,
        description=(
            "WASM 64KiB pages cap override (explicit override wins tier defaults). "
            "Examples: micro=4096 (~256MiB), meso=32768 (~2GiB), macro=131072 (~8GiB), "
            "workgroup=262144 (~16GiB), enterprise_core=4194304 (~256GiB)."
        ),
    )
    wasm_memory64_max_mb: Optional[float] = Field(
        None,
        ge=0.0,
        description=(
            "Host/runtime linear memory ceiling for Memory64 tiers. "
            "Defaults by tier when unset: macro~8192, workgroup~16384, enterprise_core~262144."
        ),
    )
    use_memory64: Optional[bool] = Field(
        None, description="Prefer Memory64 linear memory when runtime supports it"
    )

    @field_validator("enclave_tier", mode="before")
    @classmethod
    def _normalize_enclave_tier(cls, v: Any) -> Optional[str]:
        from qminiwasm.config import normalize_enclave_tier_value

        return normalize_enclave_tier_value(v)


class RunpodServerlessSection(BaseModel):
    """Optional WUI / RunPod Serverless hints (ignored by the training engine)."""

    model_config = ConfigDict(extra="forbid")

    worker_image: Optional[str] = None


class TrainingConfig(BaseModel):
    """Root TOML document for training."""

    model_config = ConfigDict(extra="forbid")

    hardware: HardwareSection = Field(default_factory=HardwareSection)
    model: ModelSection = Field(default_factory=ModelSection)
    distributed: DistributedSection = Field(default_factory=DistributedSection)
    training: TrainingSection = Field(default_factory=TrainingSection)
    data: DataSection = Field(default_factory=DataSection)
    huggingface: HuggingFaceSection = Field(default_factory=HuggingFaceSection)
    checkpoint: CheckpointSection = Field(default_factory=CheckpointSection)
    tpem: TpemSection = Field(default_factory=TpemSection)
    eval: EvalSection = Field(default_factory=EvalSection)
    adapter: AdapterSection = Field(default_factory=AdapterSection)
    cascade: CascadeSection = Field(default_factory=CascadeSection)
    #: Ordered curriculum phases; when non-empty, epoch count is ``sum(epochs)`` unless overridden.
    training_phases: Optional[List[TrainingPhaseSection]] = None
    cascade_curriculum_loop: CascadeCurriculumLoopSection = Field(
        default_factory=CascadeCurriculumLoopSection
    )
    wasm: WasmSection = Field(default_factory=WasmSection)
    serve: ServeSection = Field(default_factory=ServeSection)
    enclave: EnclaveSection = Field(default_factory=EnclaveSection)
    runpod_serverless: Optional[RunpodServerlessSection] = None

    def to_engine_kwargs(self) -> dict[str, Any]:
        """Map nested sections to EngineConfig keyword argument names (omit None)."""
        out: dict[str, Any] = {}
        m = self.model.model_dump(exclude_none=True)
        if "d_model" in m:
            out["d_model"] = m["d_model"]
        if "num_ternary_blocks" in m:
            out["num_ternary_blocks"] = m["num_ternary_blocks"]
        if "io_d_model" in m:
            out["io_d_model"] = m["io_d_model"]
        if "tropical_attn_per_block" in m:
            out["tropical_attn_per_block"] = m["tropical_attn_per_block"]
        if "attention_backend" in m:
            out["attention_backend"] = m["attention_backend"]

        dist = self.distributed.model_dump(exclude_none=True)
        if "gradient_checkpointing" in dist:
            out["gradient_checkpointing"] = dist["gradient_checkpointing"]
        if "gradient_accumulation_steps" in dist:
            out["gradient_accumulation_steps"] = dist["gradient_accumulation_steps"]
        if "amp" in dist:
            out["amp_enabled"] = dist["amp"]
        if "fsdp" in dist:
            out["fsdp_enabled"] = dist["fsdp"]
        if "ddp" in dist:
            out["ddp_enabled"] = dist["ddp"]

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
        if "qaoa_simulator_backend" in h:
            out["qaoa_simulator_backend"] = h["qaoa_simulator_backend"]
        if "qaoa_mps_max_bond_dim" in h:
            out["qaoa_mps_max_bond_dim"] = h["qaoa_mps_max_bond_dim"]
        if "qaoa_prune_enabled" in h:
            out["qaoa_prune_enabled"] = h["qaoa_prune_enabled"]
        if "qaoa_prune_threshold" in h:
            out["qaoa_prune_threshold"] = h["qaoa_prune_threshold"]
        if "qaoa_prune_min_nodes" in h:
            out["qaoa_prune_min_nodes"] = h["qaoa_prune_min_nodes"]
        if "qaoa_warm_start_cache_ttl" in h:
            out["qaoa_warm_start_cache_ttl"] = h["qaoa_warm_start_cache_ttl"]
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
            "dataset_revision": "hf_dataset_revision",
            "num_samples": "hf_num_samples",
            "split": "hf_split",
            "text_fields": "hf_text_fields",
            "context_fields": "hf_context_fields",
            "wasi_slice_only": "hf_wasi_slice_only",
            "wasi_max_scan": "hf_wasi_max_scan",
            "mesh_blend_fraction": "hf_mesh_blend_fraction",
            "extra_specs": "hf_extra_specs",
            "streaming": "hf_streaming",
            "max_scan_rows": "hf_max_scan_rows",
            "max_buffered_rows": "hf_max_buffered_rows",
            "text_truncate_bytes": "hf_text_truncate_bytes",
            "deterministic_keep_every_n": "hf_deterministic_keep_every_n",
        }
        for k, v in hf.items():
            if k in key_map:
                out[key_map[k]] = v
            else:
                out[k] = v

        ck = self.checkpoint.model_dump(exclude_none=True)
        tp = self.tpem.model_dump(exclude_none=True)
        for k, v in tp.items():
            if v is not None:
                ck[k] = v
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
            "early_stop_patience": "eval_early_stop_patience",
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
            "policy_optimizer": "cascade_policy_optimizer",
            "cispo_clip_epsilon": "cispo_clip_epsilon",
        }
        for k, v in c.items():
            out[c_map[k]] = v

        if self.training_phases:
            out["training_phases"] = [p.model_dump() for p in self.training_phases]

        w = self.wasm.model_dump(exclude_none=True)
        wmap = {
            "store_memory_limit_mb": "wasm_store_memory_limit_mb",
            "fallback_policy": "wasm_fallback_policy",
            "backend": "wasm_backend",
            "force_mock": "wasm_force_mock",
            "store_instance_limit": "wasm_store_instance_limit",
            "store_memories_limit": "wasm_store_memories_limit",
        }
        for k, v in w.items():
            out[wmap[k]] = v

        e = self.enclave.model_dump(exclude_none=True)
        emap = {
            "enclave_footprint_mb": "enclave_footprint_mb",
            "enclave_tier": "enclave_tier",
            "certainty_scalar_threshold": "certainty_scalar_threshold",
            "max_linear_memory_pages": "max_linear_memory_pages",
            "wasm_memory64_max_mb": "wasm_memory64_max_mb",
            "use_memory64": "use_memory64",
        }
        for k, v in e.items():
            out[emap[k]] = v

        return out


def load_training_toml(path: str | Path) -> TrainingConfig:
    """Parse and validate a training TOML file."""
    p = Path(path)
    raw = p.read_bytes()
    data = tomllib.loads(raw.decode("utf-8"))
    return TrainingConfig.model_validate(data)


def load_serve_document(path: str | Path) -> TrainingConfig:
    """Parse a TOML file that may contain ``[serve]`` and ``[enclave]`` (and other tables)."""
    p = Path(path)
    data = tomllib.loads(p.read_bytes().decode("utf-8"))
    return TrainingConfig.model_validate(data)


def load_serve_toml(path: str | Path) -> ServeSection:
    """Load only the [serve] table (file may contain training sections too)."""
    return load_serve_document(path).serve


def load_enclave_toml(path: str | Path) -> EnclaveSection:
    """Load only the [enclave] table from a serve/config TOML."""
    return load_serve_document(path).enclave
