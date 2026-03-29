"""Training entrypoint: load config from env and run the training loop."""

from __future__ import annotations

from ._dotenv import load_dotenv_if_available
from .config import EngineConfig
from qminiwasm.runtime_modes import apply_optimized_auto_defaults
from qminiwasm.training.loop import run_training_loop


def main(config: EngineConfig | None = None, *, wui_stop_file: str | None = None) -> dict:
    """Run the training loop with the given or env-derived config."""
    load_dotenv_if_available()
    apply_optimized_auto_defaults()
    if config is None:
        config = EngineConfig()
    wasm_kw = config.wasm_runtime_kwargs()
    rt = wasm_kw["runtime"]
    export_runtime_policy = {
        "enclave_tier": rt.enclave_tier or getattr(config, "enclave_tier", None),
        "use_memory64": bool(rt.use_memory64),
        "wasm_memory64_max_mb": rt.memory64_max_mb,
        "store_memory_limit_bytes": int(rt.store_memory_limit_bytes),
    }
    mesh_algos = None
    if getattr(config, "mesh_algorithms", None):
        mesh_algos = [a.strip() for a in str(config.mesh_algorithms).split(",") if a.strip()]

    _lp = getattr(config, "lr_plateau_patience", None)
    lr_plateau_patience = _lp if _lp and _lp > 0 else None

    return run_training_loop(
        epochs=config.epochs,
        batch_size=config.batch_size,
        dataloader_num_workers=getattr(config, "dataloader_num_workers", None),
        learning_rate=config.learning_rate,
        accelerator=config.accelerator,
        device_index=config.device_index,
        quantum_backend=config.quantum_backend,
        num_qubits=config.num_qubits,
        qaoa_layers=config.qaoa_layers,
        qaoa_execution_mode=str(getattr(config, "qaoa_execution_mode", "pennylane")),
        ibm_qaoa_shots=int(getattr(config, "ibm_qaoa_shots", 1024)),
        qaoa_simulator_backend=str(getattr(config, "qaoa_simulator_backend", "auto") or "auto"),
        qaoa_mps_max_bond_dim=getattr(config, "qaoa_mps_max_bond_dim", None),
        qaoa_prune_enabled=bool(getattr(config, "qaoa_prune_enabled", False)),
        qaoa_prune_threshold=float(getattr(config, "qaoa_prune_threshold", 0.0) or 0.0),
        qaoa_prune_min_nodes=int(getattr(config, "qaoa_prune_min_nodes", 4) or 4),
        qaoa_warm_start_cache_ttl=int(getattr(config, "qaoa_warm_start_cache_ttl", 128) or 128),
        data_path=config.data_path,
        training_data_source=getattr(config, "training_data_source", "mesh"),
        mesh_algorithms=mesh_algos,
        hf_dataset_config=getattr(config, "hf_dataset_config", None),
        hf_num_samples=getattr(config, "hf_num_samples", None),
        hf_split=getattr(config, "hf_split", "train"),
        hf_text_fields=getattr(config, "hf_text_fields", None),
        hf_context_fields=getattr(config, "hf_context_fields", None),
        hf_wasi_slice_only=bool(getattr(config, "hf_wasi_slice_only", False)),
        hf_wasi_max_scan=getattr(config, "hf_wasi_max_scan", None),
        hf_token=getattr(config, "hf_token", None),
        seed=getattr(config, "seed", None),
        grad_clip_norm=getattr(config, "grad_clip_norm", None),
        lr_plateau_patience=lr_plateau_patience,
        lr_plateau_factor=float(getattr(config, "lr_plateau_factor", 0.5)),
        lr_plateau_min_lr=float(getattr(config, "lr_plateau_min_lr", 1e-7)),
        early_stop_patience=(
            int(p)
            if (p := getattr(config, "early_stop_patience", None)) is not None and p > 0
            else None
        ),
        checkpoint_load_path=getattr(config, "checkpoint_load_path", None),
        checkpoint_save_path=getattr(config, "checkpoint_save_path", None),
        checkpoint_best_path=getattr(config, "checkpoint_best_path", None),
        checkpoint_latest_path=getattr(config, "checkpoint_latest_path", None),
        eval_holdout_fraction=float(getattr(config, "eval_holdout_fraction", 0.0) or 0.0),
        eval_every_epoch=bool(getattr(config, "eval_every_epoch", False)),
        eval_early_stop_patience=(
            int(p)
            if (p := getattr(config, "eval_early_stop_patience", None)) is not None and p > 0
            else None
        ),
        target_mean_mse=(
            float(_tm) if (_tm := getattr(config, "target_mean_mse", None)) is not None else None
        ),
        stop_on_target_mse=bool(getattr(config, "stop_on_target_mse", False)),
        hybrid_adapter=bool(getattr(config, "hybrid_adapter", False)),
        hybrid_adapter_hidden=int(getattr(config, "hybrid_adapter_hidden", 1024)),
        tequila_deadzone=float(getattr(config, "tequila_deadzone", 0.0) or 0.0),
        lota_rank=int(getattr(config, "lota_rank", 0) or 0),
        use_tsign_ternary=bool(getattr(config, "use_tsign_ternary", False)),
        tsign_learning_rate=float(getattr(config, "tsign_learning_rate", 1e-3) or 1e-3),
        lota_merge_every_epoch=bool(getattr(config, "lota_merge_every_epoch", False)),
        use_cascade_rl=bool(getattr(config, "use_cascade_rl", True)),
        cascade_policy_lr=float(getattr(config, "cascade_policy_lr", config.learning_rate)),
        cascade_steps_per_epoch=int(getattr(config, "cascade_steps_per_epoch", 2)),
        cascade_group_size=int(getattr(config, "cascade_group_size", 4)),
        cascade_state_dim=int(getattr(config, "cascade_state_dim", 8)),
        cascade_num_actions=int(getattr(config, "cascade_num_actions", 4)),
        cascade_mopd_lambda=float(getattr(config, "cascade_mopd_lambda", 0.0) or 0.0),
        cascade_mopd_feat_loss=str(getattr(config, "cascade_mopd_feat_loss", "mse") or "mse"),
        cascade_seed_from_hidden=bool(getattr(config, "cascade_seed_from_hidden", True)),
        use_cascade_router=bool(getattr(config, "use_cascade_router", False)),
        cascade_learned_projector=bool(getattr(config, "cascade_learned_projector", False)),
        cascade_router_hidden=int(getattr(config, "cascade_router_hidden", 32) or 32),
        cascade_couple_forward=bool(getattr(config, "cascade_couple_forward", True)),
        hf_mesh_blend_fraction=float(getattr(config, "hf_mesh_blend_fraction", 0.0) or 0.0),
        hf_extra_specs=getattr(config, "hf_extra_specs", None),
        hf_streaming=bool(getattr(config, "hf_streaming", False)),
        hf_max_scan_rows=getattr(config, "hf_max_scan_rows", None),
        hf_max_buffered_rows=getattr(config, "hf_max_buffered_rows", None),
        hf_text_truncate_bytes=getattr(config, "hf_text_truncate_bytes", None),
        hf_deterministic_keep_every_n=getattr(config, "hf_deterministic_keep_every_n", None),
        hf_dataset_revision=str(getattr(config, "hf_dataset_revision", None) or "main"),
        wasm_runtime=wasm_kw["runtime"],
        enclave_tier=getattr(config, "enclave_tier", None),
        enclave_footprint_mb=getattr(config, "enclave_footprint_mb", None),
        export_runtime_policy=export_runtime_policy,
        log_xpu_memory=bool(getattr(config, "log_xpu_memory", False)),
        log_xpu_memory_reset_peak=bool(getattr(config, "log_xpu_memory_reset_peak", False)),
        log_train_throughput=bool(getattr(config, "log_train_throughput", False)),
        wui_stop_file=wui_stop_file,
        d_model=int(getattr(config, "d_model", 4096) or 4096),
        num_ternary_blocks=int(getattr(config, "num_ternary_blocks", 1) or 1),
        io_d_model=int(getattr(config, "io_d_model", 4096) or 4096),
        tropical_attn_per_block=bool(getattr(config, "tropical_attn_per_block", False)),
        gradient_checkpointing=bool(getattr(config, "gradient_checkpointing", False)),
        gradient_accumulation_steps=int(getattr(config, "gradient_accumulation_steps", 1) or 1),
        amp_enabled=bool(getattr(config, "amp_enabled", False)),
        fsdp_enabled=bool(getattr(config, "fsdp_enabled", False)),
        ddp_enabled=bool(getattr(config, "ddp_enabled", False)),
    )
