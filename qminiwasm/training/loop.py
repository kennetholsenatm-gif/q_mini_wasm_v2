"""PyTorch training loop for the classical ML components with quantum routing.

Orchestrates device selection (CUDA/ARC/CPU), model forward through the quantum
router, loss, backward (parameter-shift through the circuit), and optimizer step.
Uses DataPipeline and Wasmtime traces when available; STE is handled by TernaryWASMExpert.
"""

from __future__ import annotations

import inspect
import logging
import math
import random
from typing import Any, Callable, Dict, List, Literal, Optional, Tuple, cast

import torch
import torch.nn as nn
import torch.nn.functional as F

from ..data.pipeline import DataPipeline
from ..hardware.device import AcceleratorType, get_device
from ..model import QMiniWASM
from .cascade_mopd_teacher import build_noise_state_mopd_fns
from .cascade_rl import (
    CascadeRouter,
    TinyCascadePolicy,
    ToyRoutingEnv,
    cascade_rl_train_step,
    hidden_digest_for_cascade,
)
from .checkpoint import (
    load_cascade_policy_from_checkpoint,
    load_checkpoint_into_model,
    save_checkpoint,
)
from .distillation import MOPDLoss, MOPDLossConfig
from .lota_qaf import TSignSGD
from .repro import set_training_seed

from ..rl.cascade_grpo import CascadeGRPO, CascadeGRPOConfig

logger = logging.getLogger(__name__)

_D_MODEL = 4096


def _normalize_cascade_mopd_feat_loss(name: str) -> str:
    v = (name or "mse").strip().lower()
    if v in ("cosine", "cos"):
        return "cosine"
    return "mse"


def _cascade_digest_from_blend(
    blend_1d: torch.Tensor,
    *,
    cascade_policy: nn.Module,
    state_dim: int,
    device: torch.device,
) -> torch.Tensor:
    """Map a single 4096-d blend vector (e.g. input/output mean mix) to toy MDP state."""
    v = blend_1d.detach()
    if isinstance(cascade_policy, CascadeRouter):
        return cascade_policy.project_hidden(v).detach()
    return hidden_digest_for_cascade(
        v,
        state_dim=int(state_dim),
        d_model=_D_MODEL,
        device=device,
    )


def _clip_grad_norm_xpu_safe(
    parameters: List[torch.nn.Parameter],
    max_norm: float,
    device: torch.device,
) -> None:
    """Global grad clip; Intel XPU + Level Zero often fails on ``foreach`` fused ops inside clip."""
    kwargs: dict[str, Any] = {}
    if device.type == "xpu":
        try:
            sig = inspect.signature(torch.nn.utils.clip_grad_norm_)
        except (TypeError, ValueError):
            sig = None
        if sig is not None and "foreach" in sig.parameters:
            kwargs["foreach"] = False
    torch.nn.utils.clip_grad_norm_(parameters, max_norm, **kwargs)


# Hugging Face: when hf_num_samples is None, load a larger slice than mesh/corpus (still cap download size).
_HF_AUTO_SAMPLES_FLOOR = 32_768
_HF_AUTO_SAMPLES_CEIL = 300_000
_HF_AUTO_SAMPLES_BATCH_MULT = 512

_HF_MAX_DATASETS_PER_RUN = 9

# Reserved ``data.path`` / ``DATA_PATH`` values: do not call ``load_dataset`` on these; merge only
# ``[huggingface].extra_specs`` (multi-dataset / extras-only mode). Case-insensitive.
_HF_MULTI_PRIMARY_PLACEHOLDERS = frozenset(
    {
        "qminiwasm/hf-multi",
        "qminiwasm/multi",
    }
)

TrainingDataSource = Literal["mesh", "corpus", "hf_tabular"]


def _is_hf_multi_primary_placeholder(primary_id: str) -> bool:
    """True when ``data.path`` is a reserved id that skips the primary Hub load (extras only)."""
    return (primary_id or "").strip().lower() in _HF_MULTI_PRIMARY_PLACEHOLDERS


def _split_int_budget(total: int, parts: int) -> List[int]:
    """Split ``total`` into ``parts`` non-negative integers that sum to ``total`` (at least ``parts`` when total allows)."""
    if parts <= 0:
        return []
    total = max(int(total), 0)
    if total < parts:
        total = parts
    base, rem = divmod(total, parts)
    return [base + (1 if i < rem else 0) for i in range(parts)]


def _merge_hf_dataset_specs(
    primary_id: str,
    primary_config: Optional[str],
    extras: Optional[List[Dict[str, Any]]],
) -> List[Tuple[str, Optional[str]]]:
    """Primary first, then extras; dedupe by (id, config); cap at :data:`_HF_MAX_DATASETS_PER_RUN`.

    If ``primary_id`` is a :data:`_HF_MULTI_PRIMARY_PLACEHOLDERS` entry, the primary slot does not
    load from the Hub; only ``extras`` are used (multi-dataset runs without a "main" dataset id).
    """
    primary_id = (primary_id or "").strip()
    if not primary_id:
        return []
    pc = (primary_config or "").strip() or None
    seen: set[Tuple[str, str]] = set()
    out: List[Tuple[str, Optional[str]]] = []

    def add(ds_id: str, cfg: Optional[str]) -> None:
        ds_id = (ds_id or "").strip()
        if not ds_id:
            return
        key = (ds_id, cfg or "")
        if key in seen:
            return
        seen.add(key)
        out.append((ds_id, cfg))

    if not _is_hf_multi_primary_placeholder(primary_id):
        add(primary_id, pc)
    for row in extras or []:
        if not isinstance(row, dict):
            continue
        eid = str(row.get("path") or "").strip()
        if not eid:
            continue
        raw = row.get("dataset_config")
        ecfg = (str(raw).strip() or None) if raw is not None else None
        add(eid, ecfg)
        if len(out) >= _HF_MAX_DATASETS_PER_RUN:
            break
    if len(out) > _HF_MAX_DATASETS_PER_RUN:
        logger.warning(
            "HF multi: truncating to %s datasets (had more in config)",
            _HF_MAX_DATASETS_PER_RUN,
        )
        return out[:_HF_MAX_DATASETS_PER_RUN]
    return out


def split_train_eval_data(
    processed_data: List[Dict[str, Any]],
    holdout_fraction: float,
    seed: Optional[int],
) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
    """Split samples for holdout eval. With ``seed``, shuffle indices deterministically; else last ``k`` rows are eval."""
    n = len(processed_data)
    frac = float(holdout_fraction)
    if frac <= 0.0 or frac >= 1.0 or n < 2:
        return processed_data, []

    k = max(1, int(n * frac))
    if k >= n:
        logger.warning(
            "EVAL_HOLDOUT_FRACTION yields k=%s >= n=%s; skipping holdout split.",
            k,
            n,
        )
        return processed_data, []

    if seed is not None:
        rng = random.Random(int(seed))
        order = list(range(n))
        rng.shuffle(order)
        eval_ix = order[:k]
        train_ix = order[k:]
        eval_samples = [processed_data[i] for i in eval_ix]
        train_samples = [processed_data[i] for i in train_ix]
        return train_samples, eval_samples

    train_samples = processed_data[:-k]
    eval_samples = processed_data[-k:]
    return train_samples, eval_samples


def _mean_mse_on_batches(
    model: QMiniWASM,
    samples: List[Dict[str, Any]],
    batch_size: int,
    device: torch.device,
    as_d_model: Callable[[str, torch.Tensor], torch.Tensor],
) -> float:
    """Mean batch MSE of ``hybrid_inference`` vs ``target`` (no grad)."""
    if not samples:
        return float("nan")
    # QMiniWASM is not an nn.Module; only trainable submodules have .train() / .training.
    was_qr = model.quantum_router.training
    was_te = model.ternary_expert.training
    model.quantum_router.eval()
    model.ternary_expert.eval()
    total = 0.0
    n_batches = 0
    with torch.no_grad():
        for i in range(0, len(samples), batch_size):
            batch = samples[i : i + batch_size]
            if not batch:
                continue
            hidden_list = [as_d_model("hidden", b["hidden"]) for b in batch]
            target_list = [as_d_model("target", b["target"]) for b in batch]
            hidden_states = torch.stack(hidden_list).to(device)
            targets = torch.stack(target_list).to(device)
            out = model.hybrid_inference(hidden_states)
            total += F.mse_loss(out, targets).item()
            n_batches += 1
    if was_qr:
        model.quantum_router.train()
    if was_te:
        model.ternary_expert.train()
    return total / n_batches if n_batches else float("nan")


def run_training_loop(
    epochs: int = 25,
    batch_size: int = 32,
    learning_rate: float = 1e-4,
    accelerator: str | None = None,
    device_index: int | None = None,
    quantum_backend: str = "penny_lane",
    num_qubits: int = 8,
    qaoa_layers: int = 3,
    data_path: str | None = None,
    training_data_source: TrainingDataSource | str = "mesh",
    mesh_algorithms: List[str] | None = None,
    hf_dataset_config: str | None = None,
    hf_num_samples: int | None = None,
    hf_split: str = "train",
    hf_text_fields: List[str] | None = None,
    hf_context_fields: Optional[List[str]] = None,
    hf_wasi_slice_only: bool = False,
    hf_wasi_max_scan: int | None = None,
    hf_token: str | None = None,
    seed: int | None = None,
    grad_clip_norm: float | None = None,
    lr_plateau_patience: int | None = None,
    lr_plateau_factor: float = 0.5,
    lr_plateau_min_lr: float = 1e-7,
    early_stop_patience: int | None = None,
    checkpoint_load_path: str | None = None,
    checkpoint_save_path: str | None = None,
    checkpoint_best_path: str | None = None,
    checkpoint_latest_path: str | None = None,
    eval_holdout_fraction: float = 0.0,
    eval_every_epoch: bool = False,
    target_mean_mse: float | None = None,
    stop_on_target_mse: bool = False,
    hybrid_adapter: bool = False,
    hybrid_adapter_hidden: int = 1024,
    tequila_deadzone: float = 0.0,
    lota_rank: int = 0,
    use_tsign_ternary: bool = False,
    tsign_learning_rate: float = 1e-3,
    lota_merge_every_epoch: bool = False,
    use_cascade_rl: bool = True,
    cascade_policy_lr: float = 1e-4,
    cascade_steps_per_epoch: int = 2,
    cascade_group_size: int = 4,
    cascade_state_dim: int = 8,
    cascade_num_actions: int = 4,
    cascade_mopd_lambda: float = 0.0,
    cascade_mopd_feat_loss: str = "mse",
    cascade_seed_from_hidden: bool = True,
    use_cascade_router: bool = False,
    cascade_learned_projector: bool = False,
    cascade_router_hidden: int = 32,
    cascade_couple_forward: bool = True,
    hf_mesh_blend_fraction: float = 0.0,
    hf_extra_specs: Optional[List[Dict[str, Any]]] = None,
    hf_streaming: bool = False,
    hf_max_scan_rows: int | None = None,
    hf_max_buffered_rows: int | None = None,
    hf_text_truncate_bytes: int | None = None,
    hf_deterministic_keep_every_n: int | None = None,
    qaoa_execution_mode: str = "pennylane",
    ibm_qaoa_shots: int = 1024,
    qaoa_simulator_backend: str = "auto",
    qaoa_mps_max_bond_dim: int | None = None,
    qaoa_prune_enabled: bool = False,
    qaoa_prune_threshold: float = 0.0,
    qaoa_prune_min_nodes: int = 4,
    qaoa_warm_start_cache_ttl: int = 128,
) -> dict[str, Any]:
    """Run the training curriculum for QMiniWASM.

    Args:
        epochs: Number of training epochs.
        batch_size: Batch size for the dataloader.
        learning_rate: AdamW learning rate.
        accelerator: "cuda", "xpu", or "cpu"; if None, use env PREFER_XPU / PREFER_CUDA.
        device_index: Device index for cuda/xpu.
        quantum_backend: Logical backend name (also used with IBM device selection).
        num_qubits: QAOA width when ``qaoa_execution_mode`` is ``qiskit_*``.
        qaoa_layers: QAOA depth when ``qaoa_execution_mode`` is ``qiskit_*``.
        qaoa_execution_mode: ``pennylane`` (identity MoE), ``qiskit_statevector`` (local exact Qiskit),
            ``qiskit_ibm`` (IBM Quantum Runtime Estimator; requires token and ``IBM_BACKEND_NAME``).
        ibm_qaoa_shots: Estimator precision hint for IBM mode (mapped to ``precision``).
        data_path: Path to corpus manifest (when training_data_source is ``corpus``)
            or Hugging Face dataset id (when ``hf_tabular``).
        training_data_source: ``mesh`` (embedded C/WASM curriculum), ``corpus`` (manifest JSON),
            or ``hf_tabular`` (row encoding; not WASM-semantics — see hf_loader module doc).
        mesh_algorithms: Subset of hash/encrypt/network/routing/consensus for mesh mode.
        hf_dataset_config: Optional HF config name for load_dataset.
        hf_num_samples: Max HF rows to load. When None, uses
        ``min(ceil, max(floor, batch_size * mult))`` (see module constants) for a larger HF run.
        hf_split: HF split name (e.g. ``train``).
        hf_text_fields: Column names to concatenate for encoding; None uses auto-detect / legacy.
        hf_context_fields: Optional labeled metadata keys prepended before code (see hf_loader). None
            uses defaults in auto mode; ``[]`` disables; non-empty list selects keys (e.g. CodeSearchNet).
        hf_token: Optional Hugging Face Hub token for ``load_dataset`` (rate limits / gated data).
        hf_extra_specs: Optional list of ``{"path": "org/ds", "dataset_config": "..."}`` entries merged
            with ``data_path`` / ``hf_dataset_config`` (max 9 Hub datasets total including primary).
        hf_wasi_slice_only: If True (env ``HF_WASI_SLICE_ONLY``), stream HF split and keep only rows
            whose encoded text references WASI (see ``hf_loader.encoded_blob_references_wasi``).
        hf_wasi_max_scan: Optional max source rows to scan when ``hf_wasi_slice_only`` (env
            ``HF_WASI_MAX_SCAN``); default inside loader is 12_000_000 when unset.
        seed: If set, seeds Python / NumPy / torch for reproducible runs (best-effort on GPU).
        grad_clip_norm: If set, clip global gradient norm after backward (AdamW stability).
        lr_plateau_patience: If > 0, use ReduceLROnPlateau on epoch mean MSE (``mode=min``).
        lr_plateau_factor: LR multiplicative factor when plateau triggers.
        lr_plateau_min_lr: Minimum LR for the scheduler.
        early_stop_patience: If > 0, stop after this many epochs without improvement on best epoch MSE.
        checkpoint_load_path: If set, load trainable weights before training.
        checkpoint_save_path: If set, save weights after training completes.
        checkpoint_best_path: If set, save when epoch mean MSE improves.
        checkpoint_latest_path: If set, overwrite this file after each epoch that ran batches
            (crash recovery; does not include optimizer state).
        eval_holdout_fraction: Fraction in ``(0,1)`` for holdout eval; ``0`` disables.
        eval_every_epoch: If True and holdout is non-empty, log eval MSE each epoch.
        target_mean_mse: Optional success threshold on mean ``F.mse_loss`` (same definition as
            logged ``mean_mse`` / holdout ``eval_mean_mse``). Example: ``1e-4`` for 0.0001.
        stop_on_target_mse: If True (env ``STOP_ON_TARGET_MSE``), end training early when the
            threshold is met. Prefers holdout **eval** mean MSE when ``eval_every_epoch`` and
            holdout are enabled; otherwise uses train epoch mean MSE (optimistic).
        hybrid_adapter: If True (env ``HYBRID_ADAPTER``), add residual MLP capacity after ternary.
        hybrid_adapter_hidden: Bottleneck width (env ``HYBRID_ADAPTER_HIDDEN``, default 1024).
        tequila_deadzone: Tequila deadzone fraction on ``TernaryWASMExpert`` (0 = off).
        lota_rank: LoRA rank for LoTA-QAF side branch (0 = off).
        use_tsign_ternary: If True, step ``ternary_expert.weight`` with :class:`TSignSGD` and
            other trainable params with AdamW.
        tsign_learning_rate: Learning rate for t-SignSGD when enabled.
        lota_merge_every_epoch: If True, merge LoRA into ternary base after each epoch.
        use_cascade_rl: If True (default), run on-policy cascade (GRPO) micro-steps on a toy
            routing MDP before each epoch's supervised MSE batches (escalation policy warm-up).
        cascade_policy_lr: Adam LR for the cascade policy (defaults to ``learning_rate`` from engine).
        cascade_steps_per_epoch: Number of ``cascade_rl_train_step`` calls per epoch.
        cascade_group_size: Trajectories per GRPO step (>=2 recommended for normalized advantages).
        cascade_state_dim / cascade_num_actions: Toy MDP shape.
        cascade_mopd_lambda: If >0, add MOPD feature loss vs noisy teacher on state embeddings.
        cascade_mopd_feat_loss: ``mse`` or ``cosine`` for MOPD feature term (see :class:`MOPDLossConfig`).
        cascade_seed_from_hidden: If True, each supervised batch updates a digest from mean
            ``hidden``; the next epoch's toy MDP resets from that digest (bridges cascade to model inputs).
        use_cascade_router: If True, attach :class:`CascadeRouter` on ``QMiniWASM`` (trained by the
            cascade optimizer, not main AdamW).
        cascade_learned_projector: If True and ``use_cascade_router`` is False, create a loop-owned
            :class:`CascadeRouter`; otherwise use :class:`TinyCascadePolicy` on latent state only.
        cascade_router_hidden: MLP hidden width inside :class:`CascadeRouter`.
        cascade_couple_forward: If True (default), cascade digest blends mean input hidden and mean
            ``hybrid_inference`` output each batch (one forward), tying the toy MDP to the live model.
        hf_mesh_blend_fraction: When ``training_data_source`` is ``hf_tabular``, append this fraction
            of the HF row count as extra mesh-generated samples (WASM curriculum) and shuffle when
            ``seed`` is set (0 disables).

    Returns:
        Dict with ``epochs_run``, ``final_loss``, ``metrics``, checkpoint path fields.
    """
    if seed is not None:
        set_training_seed(int(seed))

    device = get_device(
        accelerator=cast(AcceleratorType | None, accelerator),
        device_index=device_index,
    )

    model = QMiniWASM(
        device=device,
        use_hybrid_adapter=hybrid_adapter,
        hybrid_adapter_hidden=hybrid_adapter_hidden,
        tequila_deadzone=float(tequila_deadzone),
        lota_rank=int(lota_rank),
        use_cascade_router=bool(use_cascade_router),
        cascade_state_dim=int(cascade_state_dim),
        cascade_num_actions=int(cascade_num_actions),
        cascade_router_hidden=int(cascade_router_hidden),
        qaoa_execution_mode=str(qaoa_execution_mode or "pennylane"),
        num_qubits=int(num_qubits),
        qaoa_layers=int(qaoa_layers),
        ibm_qaoa_shots=int(ibm_qaoa_shots),
        quantum_backend=str(quantum_backend or "penny_lane"),
        qaoa_simulator_backend=str(qaoa_simulator_backend or "auto"),
        qaoa_mps_max_bond_dim=(
            int(qaoa_mps_max_bond_dim) if qaoa_mps_max_bond_dim is not None else None
        ),
        qaoa_prune_enabled=bool(qaoa_prune_enabled),
        qaoa_prune_threshold=float(qaoa_prune_threshold),
        qaoa_prune_min_nodes=max(1, int(qaoa_prune_min_nodes)),
        qaoa_warm_start_cache_ttl=max(1, int(qaoa_warm_start_cache_ttl)),
    )
    model.quantum_router.train()
    if hasattr(model, "ternary_expert"):
        model.ternary_expert.train()

    checkpoint_saved: str | None = None
    checkpoint_best_saved: str | None = None
    checkpoint_latest_saved: str | None = None

    if checkpoint_load_path:
        try:
            load_checkpoint_into_model(model, checkpoint_load_path, map_location=device)
            logger.info("Loaded checkpoint from %s", checkpoint_load_path)
        except Exception as e:
            logger.error("Failed to load checkpoint %s: %s", checkpoint_load_path, e)
            raise

    use_tsign = bool(use_tsign_ternary)
    adam_params = (
        model.trainable_adam_parameters(exclude_ternary_weight=True)
        if use_tsign
        else model.trainable_hybrid_backbone_parameters()
    )
    optimizer = torch.optim.AdamW(adam_params, lr=learning_rate)
    tsign_opt: Optional[TSignSGD] = None
    if use_tsign:
        tsign_opt = TSignSGD([model.ternary_expert.weight], lr=float(tsign_learning_rate))

    scheduler: Optional[torch.optim.lr_scheduler.ReduceLROnPlateau] = None
    if lr_plateau_patience is not None and lr_plateau_patience > 0:
        scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
            optimizer,
            mode="min",
            factor=lr_plateau_factor,
            patience=lr_plateau_patience,
            min_lr=lr_plateau_min_lr,
        )

    pipeline = (
        model.data_pipeline if getattr(model, "data_pipeline", None) is not None else DataPipeline()
    )
    source = (training_data_source or "mesh").strip().lower()
    num_samples = max(1, batch_size * 4)

    corpus_seed = int(seed) if seed is not None else 42

    if source == "corpus":
        if not data_path:
            raise ValueError(
                "data_path must point to a corpus manifest JSON when training_data_source=corpus"
            )
        processed_data = pipeline.generate_training_data_from_corpus(
            data_path, num_samples=num_samples, seed=corpus_seed
        )
    elif source == "hf_tabular":
        dp_hf = str(data_path or "").strip()
        if not dp_hf:
            raise ValueError(
                "data_path must be a Hugging Face dataset id when training_data_source=hf_tabular"
            )
        from .hf_loader import load_hf_tabular_samples

        if hf_num_samples is not None:
            hf_n = max(1, hf_num_samples)
        else:
            hf_n = min(
                _HF_AUTO_SAMPLES_CEIL,
                max(
                    _HF_AUTO_SAMPLES_FLOOR,
                    batch_size * _HF_AUTO_SAMPLES_BATCH_MULT,
                ),
            )
        specs = _merge_hf_dataset_specs(
            dp_hf,
            hf_dataset_config,
            hf_extra_specs,
        )
        if not specs:
            if _is_hf_multi_primary_placeholder(dp_hf):
                raise ValueError(
                    "data.path is qminiwasm/hf-multi (or qminiwasm/multi) but no Hub datasets were "
                    "merged: add [huggingface].extra_specs in the training TOML, or set HF_EXTRA_SPECS "
                    'to a JSON array, e.g. [{"path":"username/dataset"}].'
                )
            raise ValueError(
                "data_path must be a Hugging Face dataset id when training_data_source=hf_tabular, "
                "or use qminiwasm/hf-multi with at least one [huggingface].extra_specs entry "
                "(extras-only multi-dataset mode)."
            )
        budgets = _split_int_budget(hf_n, len(specs))
        merged: List[Dict[str, Any]] = []
        for (ds_id, cfg_name), budget in zip(specs, budgets):
            chunk = load_hf_tabular_samples(
                ds_id,
                budget,
                split=hf_split,
                config_name=cfg_name,
                text_fields=hf_text_fields,
                context_fields=hf_context_fields,
                token=hf_token,
                wasi_slice_only=hf_wasi_slice_only,
                max_scan_rows=hf_wasi_max_scan,
                streaming=hf_streaming,
                max_scan_rows_general=hf_max_scan_rows,
                max_buffered_rows=hf_max_buffered_rows,
                text_truncate_bytes=hf_text_truncate_bytes,
                deterministic_keep_every_n=hf_deterministic_keep_every_n,
            )
            merged.extend(chunk)
        processed_data = merged
        frac = float(hf_mesh_blend_fraction)
        if frac > 0.0 and len(processed_data) > 0:
            n_extra = max(1, int(len(processed_data) * frac))
            algos_mb = mesh_algorithms or [
                "hash",
                "encrypt",
                "network",
                "routing",
                "consensus",
            ]
            mesh_samples = pipeline.generate_training_data(
                algorithms=algos_mb,
                num_samples=n_extra,
            )
            processed_data = list(processed_data) + mesh_samples
            if seed is not None:
                rng = random.Random(int(seed))
                rng.shuffle(processed_data)
    else:
        algos = mesh_algorithms or [
            "hash",
            "encrypt",
            "network",
            "routing",
            "consensus",
        ]
        processed_data = pipeline.generate_training_data(
            algorithms=algos,
            num_samples=max(1, num_samples // max(1, len(algos))),
        )

    used_random_fallback = False
    if not processed_data:
        used_random_fallback = True
        logger.warning(
            "No training samples from data source %r; using random fallback tensors. "
            "Check data_path, HF download, corpus manifest, or mesh compilation (clang).",
            source,
        )
        g = torch.Generator()
        if seed is not None:
            g.manual_seed(int(seed))
        processed_data = [
            {
                "hidden": torch.randn(_D_MODEL, generator=g),
                "target": torch.randn(_D_MODEL, generator=g),
            }
            for _ in range(max(batch_size * 2, 8))
        ]

    train_samples, eval_samples = split_train_eval_data(
        processed_data,
        eval_holdout_fraction,
        seed,
    )
    if not train_samples and eval_samples:
        logger.warning("Holdout split left no training rows; using full dataset for training.")
        train_samples = processed_data
        eval_samples = []

    def _as_d_model(name: str, t: torch.Tensor) -> torch.Tensor:
        if t.dim() != 1 or t.shape[0] != _D_MODEL:
            raise ValueError(
                f"Expected {name} shape ({_D_MODEL},), got {tuple(t.shape)}; "
                "regenerate data or fix encoder."
            )
        return t.to(dtype=torch.float32)

    final_loss = 0.0
    epoch_losses: List[float] = []
    epoch_lrs: List[float] = []
    epoch_eval_mean_mse: List[float] = []
    last_n_batches = 0
    skipped_nonfinite_batches = 0
    best_mse = float("inf")
    epochs_without_improvement = 0
    epochs_completed = 0
    stopped_early = False
    stopped_on_target_mse = False
    target_mse_stop_train_metric_warned = False

    cascade_policy: nn.Module | None = None
    cascade_optimizer: torch.optim.Optimizer | None = None
    grpo_trainer = CascadeGRPO(
        CascadeGRPOConfig(normalize_advantage=bool(int(cascade_group_size) >= 2))
    )
    epoch_cascade_loss: List[float] = []
    epoch_cascade_return: List[float] = []
    digest_holder: list[torch.Tensor | None] = [None]

    cascade_policy_mode: str | None = None
    if use_cascade_rl:
        mr = getattr(model, "cascade_router", None)
        if mr is not None:
            cascade_policy = mr
            cascade_policy_mode = "model_router"
        elif bool(cascade_learned_projector):
            cascade_policy = CascadeRouter(
                d_model=_D_MODEL,
                state_dim=int(cascade_state_dim),
                num_actions=int(cascade_num_actions),
                hidden=max(8, int(cascade_router_hidden)),
            ).to(device)
            cascade_policy_mode = "loop_router"
        else:
            cascade_policy = TinyCascadePolicy(int(cascade_state_dim), int(cascade_num_actions)).to(
                device
            )
            cascade_policy_mode = "tiny_mlp"
        cascade_optimizer = torch.optim.Adam(
            cascade_policy.parameters(), lr=float(cascade_policy_lr)
        )
        if checkpoint_load_path:
            if load_cascade_policy_from_checkpoint(
                cascade_policy, checkpoint_load_path, map_location=device
            ):
                logger.info(
                    "Loaded cascade_policy weights from %s",
                    checkpoint_load_path,
                )

    def _cascade_ckpt_module() -> nn.Module | None:
        return cascade_policy if (use_cascade_rl and cascade_policy is not None) else None

    for epoch in range(epochs):
        if use_cascade_rl and int(cascade_steps_per_epoch) > 0:
            assert cascade_policy is not None and cascade_optimizer is not None
            if cascade_seed_from_hidden and train_samples:
                with torch.no_grad():
                    npre = min(batch_size, len(train_samples))
                    pre = train_samples[:npre]
                    hlist = [_as_d_model("hidden", b["hidden"]) for b in pre]
                    h_pre = torch.stack(hlist).to(device)
                    if cascade_couple_forward:
                        out_pre = model.hybrid_inference(h_pre)
                        blend0 = 0.5 * (h_pre.mean(0) + out_pre.mean(0))
                    else:
                        blend0 = h_pre.mean(0)
                    digest_holder[0] = _cascade_digest_from_blend(
                        blend0,
                        cascade_policy=cascade_policy,
                        state_dim=int(cascade_state_dim),
                        device=device,
                    )
            c_loss_acc = 0.0
            c_ret_acc = 0.0
            mopd_mod: MOPDLoss | None = None
            _mopd_feat = _normalize_cascade_mopd_feat_loss(str(cascade_mopd_feat_loss))
            if float(cascade_mopd_lambda) > 0.0:
                mopd_mod = MOPDLoss(
                    MOPDLossConfig(
                        lambda_kl=0.0,
                        lambda_feat=1.0,
                        feat_loss=_mopd_feat,
                    )
                )

            def _env_factory() -> ToyRoutingEnv:
                init = digest_holder[0] if cascade_seed_from_hidden else None
                return ToyRoutingEnv(
                    state_dim=int(cascade_state_dim),
                    num_actions=int(cascade_num_actions),
                    max_steps=16,
                    device=device,
                    initial_state=init,
                )

            _student_h, _teacher_h = build_noise_state_mopd_fns(noise_std=0.05)
            for _ in range(int(cascade_steps_per_epoch)):
                if mopd_mod is not None:
                    cm = cascade_rl_train_step(
                        cascade_policy,
                        cascade_optimizer,
                        grpo_trainer,
                        group_size=int(cascade_group_size),
                        env_factory=_env_factory,
                        mopd=mopd_mod,
                        student_hidden_fn=_student_h,
                        teacher_hidden_fn=_teacher_h,
                        lambda_mopd=float(cascade_mopd_lambda),
                    )
                else:
                    cm = cascade_rl_train_step(
                        cascade_policy,
                        cascade_optimizer,
                        grpo_trainer,
                        group_size=int(cascade_group_size),
                        env_factory=_env_factory,
                    )
                c_loss_acc += float(cm.get("loss", 0.0))
                c_ret_acc += float(cm.get("return_mean", 0.0))

            c_loss_acc /= max(1, int(cascade_steps_per_epoch))
            c_ret_acc /= max(1, int(cascade_steps_per_epoch))
            epoch_cascade_loss.append(c_loss_acc)
            epoch_cascade_return.append(c_ret_acc)
            logger.info(
                "epoch=%s/%s cascade_rl mean_loss=%.6f mean_return=%.6f (steps=%s group=%s)",
                epoch + 1,
                epochs,
                c_loss_acc,
                c_ret_acc,
                int(cascade_steps_per_epoch),
                int(cascade_group_size),
            )

        epoch_loss = 0.0
        n_batches = 0
        for i in range(0, len(train_samples), batch_size):
            batch = train_samples[i : i + batch_size]
            if not batch:
                continue
            try:
                hidden_list = [_as_d_model("hidden", b["hidden"]) for b in batch]
                target_list = [_as_d_model("target", b["target"]) for b in batch]
            except KeyError as e:
                raise KeyError(f"Batch item missing tensor key: {e}") from e

            hidden_states = torch.stack(hidden_list).to(device)
            targets = torch.stack(target_list).to(device)

            optimizer.zero_grad()
            if tsign_opt is not None:
                tsign_opt.zero_grad()
            out = model.hybrid_inference(hidden_states)
            if use_cascade_rl and cascade_seed_from_hidden:
                hm = hidden_states.mean(0).detach()
                blend = 0.5 * (hm + out.detach().mean(0)) if cascade_couple_forward else hm
                digest_holder[0] = _cascade_digest_from_blend(
                    blend,
                    cascade_policy=cascade_policy,
                    state_dim=int(cascade_state_dim),
                    device=device,
                )
            loss = torch.nn.functional.mse_loss(out, targets)
            if not torch.isfinite(loss):
                skipped_nonfinite_batches += 1
                logger.error(
                    "Non-finite loss at epoch %s batch starting index %s; skipping step.",
                    epoch,
                    i,
                )
                continue
            loss.backward()
            if grad_clip_norm is not None and grad_clip_norm > 0:
                _clip_grad_norm_xpu_safe(adam_params, grad_clip_norm, device)
            optimizer.step()
            if tsign_opt is not None:
                tsign_opt.step()

            epoch_loss += loss.item()
            n_batches += 1

        last_n_batches = n_batches
        if n_batches > 0:
            if lota_merge_every_epoch and model.lota_branch is not None:
                model.merge_lota_into_ternary()
            final_loss = epoch_loss / n_batches
            epoch_losses.append(final_loss)
            epochs_completed = epoch + 1
            current_lr = optimizer.param_groups[0]["lr"]
            epoch_lrs.append(float(current_lr))
            if scheduler is not None:
                scheduler.step(final_loss)
            improved = final_loss < best_mse - 1e-9
            if improved:
                best_mse = final_loss
                epochs_without_improvement = 0
                if checkpoint_best_path:
                    try:
                        save_checkpoint(
                            checkpoint_best_path,
                            model,
                            meta={
                                "training_data_source": source,
                                "seed": seed,
                                "epoch": epoch + 1,
                                "best_epoch_mean_mse": best_mse,
                            },
                            cascade_policy=_cascade_ckpt_module(),
                        )
                        checkpoint_best_saved = checkpoint_best_path
                    except Exception as e:
                        logger.error(
                            "Failed to write best checkpoint %s: %s", checkpoint_best_path, e
                        )
            else:
                epochs_without_improvement += 1
            logger.info(
                "epoch=%s/%s mean_mse=%.6f lr=%.2e batches=%s%s",
                epoch + 1,
                epochs,
                final_loss,
                optimizer.param_groups[0]["lr"],
                n_batches,
                " *" if improved else "",
            )
            if checkpoint_latest_path:
                try:
                    save_checkpoint(
                        checkpoint_latest_path,
                        model,
                        meta={
                            "training_data_source": source,
                            "seed": seed,
                            "epoch": epoch + 1,
                            "epochs_requested": epochs,
                            "epoch_mean_mse": final_loss,
                            "best_epoch_mean_mse": best_mse if best_mse < float("inf") else None,
                            "learning_rate": float(optimizer.param_groups[0]["lr"]),
                        },
                        cascade_policy=_cascade_ckpt_module(),
                    )
                    checkpoint_latest_saved = checkpoint_latest_path
                except Exception as e:
                    logger.error(
                        "Failed to write latest checkpoint %s: %s",
                        checkpoint_latest_path,
                        e,
                    )
            ev_this: Optional[float] = None
            if eval_every_epoch and eval_samples:
                ev_this = _mean_mse_on_batches(model, eval_samples, batch_size, device, _as_d_model)
                epoch_eval_mean_mse.append(ev_this)
                logger.info("epoch=%s eval_mean_mse=%.6f", epoch + 1, ev_this)
            if (
                target_mean_mse is not None
                and target_mean_mse > 0.0
                and stop_on_target_mse
                and math.isfinite(final_loss)
            ):
                if ev_this is not None and math.isfinite(ev_this):
                    crit = ev_this
                    crit_name = "eval_mean_mse"
                else:
                    crit = final_loss
                    crit_name = "train_mean_mse"
                    if (
                        eval_samples
                        and not eval_every_epoch
                        and not target_mse_stop_train_metric_warned
                    ):
                        logger.warning(
                            "STOP_ON_TARGET_MSE comparing %s to TARGET_MEAN_MSE=%.6e; "
                            "set EVAL_EVERY_EPOCH=1 to gate on holdout eval.",
                            crit_name,
                            target_mean_mse,
                        )
                        target_mse_stop_train_metric_warned = True
                if crit <= target_mean_mse:
                    stopped_on_target_mse = True
                    stopped_early = True
                    logger.info(
                        "Target MSE reached: %s=%.6e <= TARGET_MEAN_MSE=%.6e (epoch %s/%s).",
                        crit_name,
                        crit,
                        target_mean_mse,
                        epoch + 1,
                        epochs,
                    )
                    break
            if (
                early_stop_patience is not None
                and early_stop_patience > 0
                and epochs_without_improvement >= early_stop_patience
            ):
                stopped_early = True
                logger.info(
                    "Early stopping: no improvement for %s epochs (best mean_mse=%.6f).",
                    early_stop_patience,
                    best_mse,
                )
                break
        else:
            logger.warning(
                "epoch=%s produced zero batches; check batch_size and data length", epoch + 1
            )

    eval_mean_mse: Optional[float] = None
    if eval_samples and not eval_every_epoch:
        eval_mean_mse = _mean_mse_on_batches(model, eval_samples, batch_size, device, _as_d_model)
        logger.info("eval_mean_mse=%.6f (holdout n=%s)", eval_mean_mse, len(eval_samples))
    elif eval_samples and eval_every_epoch and epoch_eval_mean_mse:
        eval_mean_mse = epoch_eval_mean_mse[-1]

    if checkpoint_save_path:
        try:
            save_checkpoint(
                checkpoint_save_path,
                model,
                meta={
                    "training_data_source": source,
                    "seed": seed,
                    "epochs_completed": epochs_completed,
                    "final_loss": final_loss,
                    "best_epoch_mean_mse": best_mse if best_mse < float("inf") else None,
                },
                cascade_policy=_cascade_ckpt_module(),
            )
            checkpoint_saved = checkpoint_save_path
        except Exception as e:
            logger.error("Failed to save checkpoint %s: %s", checkpoint_save_path, e)
            raise

    metrics: dict[str, Any] = {
        "training_data_source": source,
        "num_samples": len(processed_data),
        "num_samples_train": len(train_samples),
        "num_samples_eval": len(eval_samples),
        "batch_size": batch_size,
        "learning_rate": learning_rate,
        "device": str(device),
        "used_random_fallback": used_random_fallback,
        "batches_last_epoch": last_n_batches,
        "epoch_mean_mse": epoch_losses,
        "epoch_learning_rates": epoch_lrs,
        "skipped_nonfinite_batches": skipped_nonfinite_batches,
        "epochs_requested": epochs,
        "epochs_completed": epochs_completed,
        "best_epoch_mean_mse": best_mse if best_mse < float("inf") else None,
        "stopped_early": stopped_early,
        "stopped_on_target_mse": stopped_on_target_mse,
    }
    if seed is not None:
        metrics["seed"] = int(seed)
    if source == "hf_tabular" and hf_wasi_slice_only:
        metrics["hf_wasi_slice_only"] = True
        if hf_wasi_max_scan is not None:
            metrics["hf_wasi_max_scan"] = int(hf_wasi_max_scan)
    if source == "hf_tabular" and float(hf_mesh_blend_fraction) > 0.0:
        metrics["hf_mesh_blend_fraction"] = float(hf_mesh_blend_fraction)
    if hybrid_adapter:
        metrics["hybrid_adapter"] = True
        metrics["hybrid_adapter_hidden"] = int(hybrid_adapter_hidden)
    if float(tequila_deadzone) > 0.0:
        metrics["tequila_deadzone"] = float(tequila_deadzone)
    if int(lota_rank) > 0:
        metrics["lota_rank"] = int(lota_rank)
        metrics["use_tsign_ternary"] = bool(use_tsign_ternary)
        metrics["lota_merge_every_epoch"] = bool(lota_merge_every_epoch)
    if use_cascade_rl:
        metrics["use_cascade_rl"] = True
        metrics["cascade_steps_per_epoch"] = int(cascade_steps_per_epoch)
        metrics["cascade_group_size"] = int(cascade_group_size)
        metrics["cascade_mopd_lambda"] = float(cascade_mopd_lambda)
        if float(cascade_mopd_lambda) > 0.0:
            metrics["cascade_mopd_feat_loss"] = _normalize_cascade_mopd_feat_loss(
                str(cascade_mopd_feat_loss)
            )
        metrics["cascade_seed_from_hidden"] = bool(cascade_seed_from_hidden)
        metrics["cascade_couple_forward"] = bool(cascade_couple_forward)
        if cascade_policy_mode is not None:
            metrics["cascade_policy_mode"] = cascade_policy_mode
        if epoch_cascade_loss:
            metrics["epoch_cascade_loss"] = epoch_cascade_loss
        if epoch_cascade_return:
            metrics["epoch_cascade_return"] = epoch_cascade_return
    if grad_clip_norm is not None:
        metrics["grad_clip_norm"] = float(grad_clip_norm)
    if scheduler is not None and lr_plateau_patience is not None:
        metrics["lr_plateau_patience"] = int(lr_plateau_patience)
        metrics["lr_plateau_factor"] = float(lr_plateau_factor)
    if early_stop_patience is not None and early_stop_patience > 0:
        metrics["early_stop_patience"] = int(early_stop_patience)

    if eval_holdout_fraction > 0:
        metrics["eval_holdout_fraction"] = float(eval_holdout_fraction)
        metrics["eval_every_epoch"] = bool(eval_every_epoch)
    if eval_mean_mse is not None and math.isfinite(eval_mean_mse):
        metrics["eval_mean_mse"] = float(eval_mean_mse)
    if epoch_eval_mean_mse:
        metrics["epoch_eval_mean_mse"] = epoch_eval_mean_mse

    if target_mean_mse is not None and target_mean_mse > 0.0:
        if eval_mean_mse is not None and math.isfinite(eval_mean_mse):
            end_crit = float(eval_mean_mse)
            end_name = "eval_mean_mse"
        else:
            end_crit = float(final_loss)
            end_name = "train_last_epoch_mean_mse"
        metrics["target_mean_mse_goal"] = float(target_mean_mse)
        metrics["target_mse_met"] = bool(end_crit <= target_mean_mse)
        metrics["target_mse_reported_value"] = end_crit
        metrics["target_mse_reported_name"] = end_name

    return {
        "epochs_run": epochs_completed,
        "final_loss": final_loss,
        "metrics": metrics,
        "checkpoint_load_path": checkpoint_load_path,
        "checkpoint_save_path": checkpoint_save_path,
        "checkpoint_best_path": checkpoint_best_path,
        "checkpoint_latest_path": checkpoint_latest_path,
        "checkpoint_saved": checkpoint_saved,
        "checkpoint_best_saved": checkpoint_best_saved,
        "checkpoint_latest_saved": checkpoint_latest_saved,
    }
