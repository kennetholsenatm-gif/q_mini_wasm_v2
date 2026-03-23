"""Engine config: TOML file + optional CLI kwargs, with env fallback and secrets from env."""

from __future__ import annotations

import logging
import os
from pathlib import Path
from typing import TYPE_CHECKING, List, Optional

from .secret_sanitize import sanitize_api_key_like

if TYPE_CHECKING:
    from .training_schema import TrainingConfig


class EngineConfig:
    """Configuration for the ML engine on the cloud instance.

    Constructor arguments that are ``None`` fall back to environment variables (legacy)
    or built-in defaults. Values passed explicitly (including from a loaded TOML file)
    take precedence over the environment for non-secret fields.
    Secrets (``hf_token``) are always merged from ``HUGGING_FACE_HUB_TOKEN`` / ``HF_TOKEN``
    when not passed explicitly.
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
        #: WUI / env QUANTUM_EXECUTION_POLICY: hardware_only | prefer_hardware_fallback | simulator_only
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
    ):
        self.accelerator = (
            accelerator
            if accelerator is not None
            else (os.environ.get("ACCELERATOR", "").strip() or None)
        )
        _idx = os.environ.get("DEVICE_INDEX", "")
        self.device_index = (
            int(device_index)
            if device_index is not None
            else (int(_idx) if _idx.isdigit() else 0)
        )
        self.quantum_backend = (
            quantum_backend
            if quantum_backend is not None
            else os.environ.get("QUANTUM_BACKEND", "penny_lane")
        )
        self.num_qubits = (
            int(num_qubits)
            if num_qubits is not None
            else int(os.environ.get("NUM_QUBITS", "8"))
        )
        self.qaoa_layers = (
            int(qaoa_layers)
            if qaoa_layers is not None
            else int(os.environ.get("QAOA_LAYERS", "3"))
        )
        _qem = (
            qaoa_execution_mode
            if qaoa_execution_mode is not None
            else os.environ.get("QMINIWASM_QAOA_EXECUTION", "pennylane")
        )
        self.qaoa_execution_mode = ((_qem or "pennylane").strip().lower())
        self.ibm_qaoa_shots = (
            int(ibm_qaoa_shots)
            if ibm_qaoa_shots is not None
            else int(os.environ.get("IBM_QAOA_SHOTS", "1024"))
        )
        self.quantum_execution_policy = (
            (quantum_execution_policy or os.environ.get("QUANTUM_EXECUTION_POLICY", "") or "")
            .strip()
            .lower()
        )
        # WUI "hardware only" must enable the Qiskit IBM QAOA path; default pennylane leaves MoE as identity.
        if self.quantum_execution_policy == "hardware_only" and self.qaoa_execution_mode == "pennylane":
            _log = logging.getLogger(__name__)
            _log.info(
                "QUANTUM_EXECUTION_POLICY=hardware_only: using qaoa_execution_mode=qiskit_ibm "
                "(default pennylane would skip real QAOA in HybridQuantumMoE)"
            )
            self.qaoa_execution_mode = "qiskit_ibm"
        self.diff_method = (
            diff_method
            if diff_method is not None
            else os.environ.get("DIFF_METHOD", "parameter-shift")
        )
        self.epochs = (
            int(epochs) if epochs is not None else int(os.environ.get("EPOCHS", "25"))
        )
        self.batch_size = (
            int(batch_size)
            if batch_size is not None
            else int(os.environ.get("BATCH_SIZE", "32"))
        )
        _lr_env = os.environ.get("LEARNING_RATE", "").strip()
        if learning_rate is not None:
            self.learning_rate = float(learning_rate)
        elif _lr_env:
            self.learning_rate = float(_lr_env)
        else:
            self.learning_rate = 1e-4

        self.data_path = (
            data_path if data_path is not None else os.environ.get("DATA_PATH")
        )
        if training_data_source is not None:
            self.training_data_source = str(training_data_source).strip() or "mesh"
        else:
            self.training_data_source = (
                os.environ.get("TRAINING_DATA_SOURCE", "mesh") or "mesh"
            ).strip()

        _tds = self.training_data_source.lower()
        if (
            learning_rate is None
            and not _lr_env
            and _tds == "hf_tabular"
            and self.learning_rate == 1e-4
        ):
            self.learning_rate = 1.5e-4

        self.mesh_algorithms = (
            mesh_algorithms
            if mesh_algorithms is not None
            else os.environ.get(
                "MESH_ALGORITHMS",
                "hash,encrypt,network,routing,consensus",
            )
        )
        self.hf_dataset_config = (
            hf_dataset_config
            if hf_dataset_config is not None
            else os.environ.get("HF_DATASET_CONFIG")
        )
        if hf_num_samples is not None:
            self.hf_num_samples = hf_num_samples
        else:
            _hns = os.environ.get("HF_NUM_SAMPLES", "").strip()
            self.hf_num_samples = int(_hns) if _hns.isdigit() else None

        if hf_split is not None:
            self.hf_split = str(hf_split).strip() or "train"
        else:
            self.hf_split = (os.environ.get("HF_SPLIT") or "train").strip() or "train"

        if hf_text_fields is not None:
            self.hf_text_fields = hf_text_fields
        else:
            _htf = os.environ.get("HF_TEXT_FIELDS", "").strip()
            self.hf_text_fields = (
                [x.strip() for x in _htf.split(",") if x.strip()] if _htf else None
            )

        if hf_context_fields is not None:
            self.hf_context_fields = hf_context_fields
        else:
            _hc = os.environ.get("HF_CONTEXT_FIELDS", "").strip().lower()
            if _hc in ("0", "false", "none", "off", "disable", "disabled"):
                self.hf_context_fields = []
            elif os.environ.get("HF_CONTEXT_FIELDS", "").strip():
                self.hf_context_fields = [
                    x.strip()
                    for x in os.environ.get("HF_CONTEXT_FIELDS", "").split(",")
                    if x.strip()
                ]
            else:
                self.hf_context_fields = None

        if hf_wasi_slice_only is not None:
            self.hf_wasi_slice_only = hf_wasi_slice_only
        else:
            _hw = os.environ.get("HF_WASI_SLICE_ONLY", "").strip().lower()
            self.hf_wasi_slice_only = _hw in ("1", "true", "yes", "on")

        if hf_wasi_max_scan is not None:
            self.hf_wasi_max_scan = hf_wasi_max_scan
        else:
            _hms = os.environ.get("HF_WASI_MAX_SCAN", "").strip()
            self.hf_wasi_max_scan = int(_hms) if _hms.isdigit() else None

        self.hf_token = hf_token
        if self.hf_token is None:
            _hub = os.environ.get("HUGGING_FACE_HUB_TOKEN", "").strip()
            _hf = os.environ.get("HF_TOKEN", "").strip()
            self.hf_token = _hub or _hf or None
        self.hf_token = sanitize_api_key_like(self.hf_token)

        if seed is not None:
            self.seed = seed
        else:
            _sd = os.environ.get("SEED", "").strip()
            if _sd:
                try:
                    self.seed = int(_sd)
                except ValueError:
                    self.seed = None
            else:
                self.seed = None

        if grad_clip_norm is not None:
            self.grad_clip_norm = grad_clip_norm if grad_clip_norm > 0 else None
        else:
            _gcn = os.environ.get("GRAD_CLIP_NORM", "").strip()
            if _gcn:
                try:
                    v = float(_gcn)
                    self.grad_clip_norm = v if v > 0 else None
                except ValueError:
                    self.grad_clip_norm = None
            elif _tds == "hf_tabular":
                self.grad_clip_norm = 1.0
            else:
                self.grad_clip_norm = None

        if lr_plateau_patience is not None:
            self.lr_plateau_patience = lr_plateau_patience
        else:
            _lp = os.environ.get("LR_PLATEAU_PATIENCE", "").strip()
            if _lp.isdigit():
                self.lr_plateau_patience = int(_lp)
            elif _lp == "0":
                self.lr_plateau_patience = 0
            elif _tds == "hf_tabular":
                self.lr_plateau_patience = 2
            else:
                self.lr_plateau_patience = None

        self.lr_plateau_factor = (
            float(lr_plateau_factor)
            if lr_plateau_factor is not None
            else float(os.environ.get("LR_PLATEAU_FACTOR", "0.5"))
        )
        self.lr_plateau_min_lr = (
            float(lr_plateau_min_lr)
            if lr_plateau_min_lr is not None
            else float(os.environ.get("LR_PLATEAU_MIN_LR", "1e-7"))
        )

        if early_stop_patience is not None:
            self.early_stop_patience = early_stop_patience
        else:
            _es = os.environ.get("EARLY_STOP_PATIENCE", "").strip()
            self.early_stop_patience = int(_es) if _es.isdigit() else None

        self.checkpoint_load_path = (
            checkpoint_load_path
            if checkpoint_load_path is not None
            else (os.environ.get("CHECKPOINT_LOAD_PATH", "").strip() or None)
        )
        self.checkpoint_save_path = (
            checkpoint_save_path
            if checkpoint_save_path is not None
            else (os.environ.get("CHECKPOINT_SAVE_PATH", "").strip() or None)
        )
        self.checkpoint_best_path = (
            checkpoint_best_path
            if checkpoint_best_path is not None
            else (os.environ.get("CHECKPOINT_BEST_PATH", "").strip() or None)
        )
        self.checkpoint_latest_path = (
            checkpoint_latest_path
            if checkpoint_latest_path is not None
            else (os.environ.get("CHECKPOINT_LATEST_PATH", "").strip() or None)
        )

        if eval_holdout_fraction is not None:
            self.eval_holdout_fraction = float(eval_holdout_fraction)
        else:
            _eh = os.environ.get("EVAL_HOLDOUT_FRACTION", "").strip()
            if _eh:
                try:
                    self.eval_holdout_fraction = float(_eh)
                except ValueError:
                    self.eval_holdout_fraction = 0.0
            else:
                self.eval_holdout_fraction = 0.0

        if eval_every_epoch is not None:
            self.eval_every_epoch = eval_every_epoch
        else:
            _ee = os.environ.get("EVAL_EVERY_EPOCH", "").strip().lower()
            self.eval_every_epoch = _ee in ("1", "true", "yes", "on")

        if target_mean_mse is not None:
            self.target_mean_mse = target_mean_mse
        else:
            _tm = os.environ.get("TARGET_MEAN_MSE", "").strip()
            if _tm:
                try:
                    self.target_mean_mse = float(_tm)
                except ValueError:
                    self.target_mean_mse = None
            elif _tds == "hf_tabular":
                self.target_mean_mse = 1e-4
            else:
                self.target_mean_mse = None

        if stop_on_target_mse is not None:
            self.stop_on_target_mse = stop_on_target_mse
        else:
            _so = os.environ.get("STOP_ON_TARGET_MSE", "").strip().lower()
            self.stop_on_target_mse = _so in ("1", "true", "yes", "on")

        if hybrid_adapter is not None:
            self.hybrid_adapter = hybrid_adapter
        else:
            _hy = os.environ.get("HYBRID_ADAPTER", "").strip().lower()
            self.hybrid_adapter = _hy in ("1", "true", "yes", "on")

        if hybrid_adapter_hidden is not None:
            self.hybrid_adapter_hidden = max(32, int(hybrid_adapter_hidden))
        else:
            _hh = os.environ.get("HYBRID_ADAPTER_HIDDEN", "").strip()
            self.hybrid_adapter_hidden = max(32, int(_hh)) if _hh.isdigit() else 1024

        if tequila_deadzone is not None:
            self.tequila_deadzone = float(tequila_deadzone)
        else:
            _td = os.environ.get("TEQUILA_DEADZONE", "").strip()
            if _td:
                try:
                    self.tequila_deadzone = float(_td)
                except ValueError:
                    self.tequila_deadzone = 0.0
            else:
                self.tequila_deadzone = 0.0

        if lota_rank is not None:
            self.lota_rank = int(lota_rank)
        else:
            _lr = os.environ.get("LOTA_RANK", "").strip()
            self.lota_rank = int(_lr) if _lr.isdigit() else 0

        if use_tsign_ternary is not None:
            self.use_tsign_ternary = use_tsign_ternary
        else:
            _ut = os.environ.get("TSIGN_SGD_TERNARY", "").strip().lower()
            self.use_tsign_ternary = _ut in ("1", "true", "yes", "on")

        if tsign_learning_rate is not None:
            self.tsign_learning_rate = float(tsign_learning_rate)
        else:
            _tsl = os.environ.get("TSIGN_LEARNING_RATE", "").strip()
            if _tsl:
                try:
                    self.tsign_learning_rate = float(_tsl)
                except ValueError:
                    self.tsign_learning_rate = 1e-3
            else:
                self.tsign_learning_rate = 1e-3

        if lota_merge_every_epoch is not None:
            self.lota_merge_every_epoch = lota_merge_every_epoch
        else:
            _lm = os.environ.get("LOTA_MERGE_EVERY_EPOCH", "").strip().lower()
            self.lota_merge_every_epoch = _lm in ("1", "true", "yes", "on")

        if use_cascade_rl is not None:
            self.use_cascade_rl = use_cascade_rl
        else:
            _cr = os.environ.get("CASCADE_RL", "").strip().lower()
            if _cr in ("0", "false", "no", "off", "disable", "disabled"):
                self.use_cascade_rl = False
            elif _cr in ("1", "true", "yes", "on"):
                self.use_cascade_rl = True
            else:
                self.use_cascade_rl = True

        if cascade_policy_lr is not None:
            self.cascade_policy_lr = float(cascade_policy_lr)
        else:
            _cpl = os.environ.get("CASCADE_POLICY_LR", "").strip()
            if _cpl:
                try:
                    self.cascade_policy_lr = float(_cpl)
                except ValueError:
                    self.cascade_policy_lr = self.learning_rate
            elif _tds == "hf_tabular":
                self.cascade_policy_lr = 0.5 * float(self.learning_rate)
            else:
                self.cascade_policy_lr = self.learning_rate

        if cascade_steps_per_epoch is not None:
            self.cascade_steps_per_epoch = int(cascade_steps_per_epoch)
        else:
            _cse = os.environ.get("CASCADE_STEPS_PER_EPOCH", "").strip()
            self.cascade_steps_per_epoch = int(_cse) if _cse.isdigit() else 2

        if cascade_group_size is not None:
            self.cascade_group_size = int(cascade_group_size)
        else:
            _cgs = os.environ.get("CASCADE_GROUP_SIZE", "").strip()
            self.cascade_group_size = int(_cgs) if _cgs.isdigit() else 4

        if cascade_state_dim is not None:
            self.cascade_state_dim = int(cascade_state_dim)
        else:
            _csd = os.environ.get("CASCADE_STATE_DIM", "").strip()
            self.cascade_state_dim = int(_csd) if _csd.isdigit() else 8

        if cascade_num_actions is not None:
            self.cascade_num_actions = int(cascade_num_actions)
        else:
            _cna = os.environ.get("CASCADE_NUM_ACTIONS", "").strip()
            self.cascade_num_actions = int(_cna) if _cna.isdigit() else 4

        if cascade_mopd_lambda is not None:
            self.cascade_mopd_lambda = float(cascade_mopd_lambda)
        else:
            _cml = os.environ.get("CASCADE_MOPD_LAMBDA", "").strip()
            if _cml:
                try:
                    self.cascade_mopd_lambda = float(_cml)
                except ValueError:
                    self.cascade_mopd_lambda = 0.0
            else:
                self.cascade_mopd_lambda = 0.0

        if cascade_mopd_feat_loss is not None:
            self.cascade_mopd_feat_loss = cascade_mopd_feat_loss
        else:
            _cmfl = os.environ.get("CASCADE_MOPD_FEAT_LOSS", "").strip().lower()
            if _cmfl in ("cosine", "cos"):
                self.cascade_mopd_feat_loss = "cosine"
            else:
                self.cascade_mopd_feat_loss = "mse"

        if cascade_seed_from_hidden is not None:
            self.cascade_seed_from_hidden = cascade_seed_from_hidden
        else:
            _csh = os.environ.get("CASCADE_SEED_FROM_HIDDEN", "").strip().lower()
            if _csh in ("0", "false", "no", "off", "disable", "disabled"):
                self.cascade_seed_from_hidden = False
            else:
                self.cascade_seed_from_hidden = True

        if use_cascade_router is not None:
            self.use_cascade_router = use_cascade_router
        else:
            _ucr = os.environ.get("USE_CASCADE_ROUTER", "").strip().lower()
            self.use_cascade_router = _ucr in ("1", "true", "yes", "on")

        if cascade_learned_projector is not None:
            self.cascade_learned_projector = cascade_learned_projector
        else:
            _clp = os.environ.get("CASCADE_LEARNED_PROJECTOR", "").strip().lower()
            self.cascade_learned_projector = _clp in ("1", "true", "yes", "on")

        if cascade_router_hidden is not None:
            self.cascade_router_hidden = int(cascade_router_hidden)
        else:
            _crh = os.environ.get("CASCADE_ROUTER_HIDDEN", "").strip()
            self.cascade_router_hidden = int(_crh) if _crh.isdigit() else 32

        if cascade_couple_forward is not None:
            self.cascade_couple_forward = cascade_couple_forward
        else:
            _ccf = os.environ.get("CASCADE_COUPLE_FORWARD", "").strip().lower()
            if _ccf in ("0", "false", "no", "off", "disable", "disabled"):
                self.cascade_couple_forward = False
            else:
                self.cascade_couple_forward = True

        if hf_mesh_blend_fraction is not None:
            self.hf_mesh_blend_fraction = max(0.0, float(hf_mesh_blend_fraction))
        else:
            _hmb = os.environ.get("HF_MESH_BLEND_FRACTION", "").strip()
            if _hmb:
                try:
                    self.hf_mesh_blend_fraction = max(0.0, float(_hmb))
                except ValueError:
                    self.hf_mesh_blend_fraction = 0.0
            else:
                self.hf_mesh_blend_fraction = 0.0

    @classmethod
    def from_training_toml(cls, path: str | Path) -> EngineConfig:
        """Load non-secret options from a TOML file; HF token still comes from env if unset."""
        from .training_schema import load_training_toml

        file_cfg = load_training_toml(path)
        kwargs = file_cfg.to_engine_kwargs()
        return cls(**kwargs)


def load_training_config(path: str | Path) -> TrainingConfig:
    """Load and validate training TOML (Pydantic root model)."""
    from .training_schema import load_training_toml

    return load_training_toml(path)
