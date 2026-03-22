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
        cascade_seed_from_hidden: Optional[bool] = None,
        use_cascade_router: Optional[bool] = None,
        cascade_learned_projector: Optional[bool] = None,
        cascade_router_hidden: Optional[int] = None,
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

        self.hf_context_fields = hf_context_fields
        if self.hf_context_fields is None:
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

        self.hf_wasi_slice_only = hf_wasi_slice_only
        if self.hf_wasi_slice_only is None:
            _hw = os.environ.get("HF_WASI_SLICE_ONLY", "").strip().lower()
            self.hf_wasi_slice_only = _hw in ("1", "true", "yes", "on")

        self.hf_wasi_max_scan = hf_wasi_max_scan
        if self.hf_wasi_max_scan is None:
            _hms = os.environ.get("HF_WASI_MAX_SCAN", "").strip()
            if _hms.isdigit():
                self.hf_wasi_max_scan = int(_hms)

        self.hf_token = hf_token
        if self.hf_token is None:
            _hub = os.environ.get("HUGGING_FACE_HUB_TOKEN", "").strip()
            _hf = os.environ.get("HF_TOKEN", "").strip()
            self.hf_token = _hub or _hf or None

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

        self.checkpoint_load_path = (
            checkpoint_load_path or os.environ.get("CHECKPOINT_LOAD_PATH", "").strip() or None
        )
        self.checkpoint_save_path = (
            checkpoint_save_path or os.environ.get("CHECKPOINT_SAVE_PATH", "").strip() or None
        )
        self.checkpoint_best_path = (
            checkpoint_best_path or os.environ.get("CHECKPOINT_BEST_PATH", "").strip() or None
        )
        self.checkpoint_latest_path = (
            checkpoint_latest_path
            or os.environ.get("CHECKPOINT_LATEST_PATH", "").strip()
            or None
        )

        self.eval_holdout_fraction = eval_holdout_fraction
        if self.eval_holdout_fraction is None:
            _eh = os.environ.get("EVAL_HOLDOUT_FRACTION", "").strip()
            if _eh:
                try:
                    self.eval_holdout_fraction = float(_eh)
                except ValueError:
                    self.eval_holdout_fraction = 0.0
            else:
                self.eval_holdout_fraction = 0.0

        self.eval_every_epoch = eval_every_epoch
        if self.eval_every_epoch is None:
            _ee = os.environ.get("EVAL_EVERY_EPOCH", "").strip().lower()
            self.eval_every_epoch = _ee in ("1", "true", "yes", "on")

        self.target_mean_mse = target_mean_mse
        if self.target_mean_mse is None:
            _tm = os.environ.get("TARGET_MEAN_MSE", "").strip()
            if _tm:
                try:
                    self.target_mean_mse = float(_tm)
                except ValueError:
                    self.target_mean_mse = None

        self.stop_on_target_mse = stop_on_target_mse
        if self.stop_on_target_mse is None:
            _so = os.environ.get("STOP_ON_TARGET_MSE", "").strip().lower()
            self.stop_on_target_mse = _so in ("1", "true", "yes", "on")

        self.hybrid_adapter = hybrid_adapter
        if self.hybrid_adapter is None:
            _hy = os.environ.get("HYBRID_ADAPTER", "").strip().lower()
            self.hybrid_adapter = _hy in ("1", "true", "yes", "on")

        self.hybrid_adapter_hidden = hybrid_adapter_hidden
        if self.hybrid_adapter_hidden is None:
            _hh = os.environ.get("HYBRID_ADAPTER_HIDDEN", "").strip()
            if _hh.isdigit():
                self.hybrid_adapter_hidden = max(32, int(_hh))
            else:
                self.hybrid_adapter_hidden = 1024

        self.tequila_deadzone = tequila_deadzone
        if self.tequila_deadzone is None:
            _td = os.environ.get("TEQUILA_DEADZONE", "").strip()
            if _td:
                try:
                    self.tequila_deadzone = float(_td)
                except ValueError:
                    self.tequila_deadzone = 0.0
            else:
                self.tequila_deadzone = 0.0

        self.lota_rank = lota_rank
        if self.lota_rank is None:
            _lr = os.environ.get("LOTA_RANK", "").strip()
            self.lota_rank = int(_lr) if _lr.isdigit() else 0

        self.use_tsign_ternary = use_tsign_ternary
        if self.use_tsign_ternary is None:
            _ut = os.environ.get("TSIGN_SGD_TERNARY", "").strip().lower()
            self.use_tsign_ternary = _ut in ("1", "true", "yes", "on")

        self.tsign_learning_rate = tsign_learning_rate
        if self.tsign_learning_rate is None:
            _tsl = os.environ.get("TSIGN_LEARNING_RATE", "").strip()
            if _tsl:
                try:
                    self.tsign_learning_rate = float(_tsl)
                except ValueError:
                    self.tsign_learning_rate = 1e-3
            else:
                self.tsign_learning_rate = 1e-3

        self.lota_merge_every_epoch = lota_merge_every_epoch
        if self.lota_merge_every_epoch is None:
            _lm = os.environ.get("LOTA_MERGE_EVERY_EPOCH", "").strip().lower()
            self.lota_merge_every_epoch = _lm in ("1", "true", "yes", "on")

        # Cascade RL (GRPO toy routing) runs before each epoch's supervised MSE phase by default.
        self.use_cascade_rl = use_cascade_rl
        if self.use_cascade_rl is None:
            _cr = os.environ.get("CASCADE_RL", "").strip().lower()
            if _cr in ("0", "false", "no", "off", "disable", "disabled"):
                self.use_cascade_rl = False
            elif _cr in ("1", "true", "yes", "on"):
                self.use_cascade_rl = True
            else:
                self.use_cascade_rl = True

        self.cascade_policy_lr = cascade_policy_lr
        if self.cascade_policy_lr is None:
            _cpl = os.environ.get("CASCADE_POLICY_LR", "").strip()
            if _cpl:
                try:
                    self.cascade_policy_lr = float(_cpl)
                except ValueError:
                    self.cascade_policy_lr = self.learning_rate
            else:
                self.cascade_policy_lr = self.learning_rate

        self.cascade_steps_per_epoch = cascade_steps_per_epoch
        if self.cascade_steps_per_epoch is None:
            _cse = os.environ.get("CASCADE_STEPS_PER_EPOCH", "").strip()
            self.cascade_steps_per_epoch = int(_cse) if _cse.isdigit() else 2

        self.cascade_group_size = cascade_group_size
        if self.cascade_group_size is None:
            _cgs = os.environ.get("CASCADE_GROUP_SIZE", "").strip()
            self.cascade_group_size = int(_cgs) if _cgs.isdigit() else 4

        self.cascade_state_dim = cascade_state_dim
        if self.cascade_state_dim is None:
            _csd = os.environ.get("CASCADE_STATE_DIM", "").strip()
            self.cascade_state_dim = int(_csd) if _csd.isdigit() else 8

        self.cascade_num_actions = cascade_num_actions
        if self.cascade_num_actions is None:
            _cna = os.environ.get("CASCADE_NUM_ACTIONS", "").strip()
            self.cascade_num_actions = int(_cna) if _cna.isdigit() else 4

        self.cascade_mopd_lambda = cascade_mopd_lambda
        if self.cascade_mopd_lambda is None:
            _cml = os.environ.get("CASCADE_MOPD_LAMBDA", "").strip()
            if _cml:
                try:
                    self.cascade_mopd_lambda = float(_cml)
                except ValueError:
                    self.cascade_mopd_lambda = 0.0
            else:
                self.cascade_mopd_lambda = 0.0

        self.cascade_seed_from_hidden = cascade_seed_from_hidden
        if self.cascade_seed_from_hidden is None:
            _csh = os.environ.get("CASCADE_SEED_FROM_HIDDEN", "").strip().lower()
            if _csh in ("0", "false", "no", "off", "disable", "disabled"):
                self.cascade_seed_from_hidden = False
            else:
                self.cascade_seed_from_hidden = True

        self.use_cascade_router = use_cascade_router
        if self.use_cascade_router is None:
            _ucr = os.environ.get("USE_CASCADE_ROUTER", "").strip().lower()
            self.use_cascade_router = _ucr in ("1", "true", "yes", "on")

        self.cascade_learned_projector = cascade_learned_projector
        if self.cascade_learned_projector is None:
            _clp = os.environ.get("CASCADE_LEARNED_PROJECTOR", "").strip().lower()
            self.cascade_learned_projector = _clp in ("1", "true", "yes", "on")

        self.cascade_router_hidden = cascade_router_hidden
        if self.cascade_router_hidden is None:
            _crh = os.environ.get("CASCADE_ROUTER_HIDDEN", "").strip()
            self.cascade_router_hidden = int(_crh) if _crh.isdigit() else 32
