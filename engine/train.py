"""Training entrypoint: load config from env and run the training loop."""

from __future__ import annotations

from ._dotenv import load_dotenv_if_available
from .config import EngineConfig
from qminiwasm.training.loop import run_training_loop


def main(config: EngineConfig | None = None) -> dict:
    """Run the training loop with the given or env-derived config."""
    load_dotenv_if_available()
    if config is None:
        config = EngineConfig()
    mesh_algos = None
    if getattr(config, "mesh_algorithms", None):
        mesh_algos = [a.strip() for a in str(config.mesh_algorithms).split(",") if a.strip()]

    _lp = getattr(config, "lr_plateau_patience", None)
    lr_plateau_patience = _lp if _lp and _lp > 0 else None

    return run_training_loop(
        epochs=config.epochs,
        batch_size=config.batch_size,
        learning_rate=config.learning_rate,
        accelerator=config.accelerator,
        device_index=config.device_index,
        quantum_backend=config.quantum_backend,
        num_qubits=config.num_qubits,
        qaoa_layers=config.qaoa_layers,
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
        target_mean_mse=(
            float(_tm)
            if (_tm := getattr(config, "target_mean_mse", None)) is not None
            else None
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
        cascade_mopd_feat_loss=str(
            getattr(config, "cascade_mopd_feat_loss", "mse") or "mse"
        ),
        cascade_seed_from_hidden=bool(getattr(config, "cascade_seed_from_hidden", True)),
        use_cascade_router=bool(getattr(config, "use_cascade_router", False)),
        cascade_learned_projector=bool(getattr(config, "cascade_learned_projector", False)),
        cascade_router_hidden=int(getattr(config, "cascade_router_hidden", 32) or 32),
        cascade_couple_forward=bool(getattr(config, "cascade_couple_forward", True)),
        hf_mesh_blend_fraction=float(
            getattr(config, "hf_mesh_blend_fraction", 0.0) or 0.0
        ),
    )
