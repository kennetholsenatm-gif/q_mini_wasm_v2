"""PyTorch training loop for the classical ML components with quantum routing.

Orchestrates device selection (CUDA/ARC/CPU), model forward through the quantum
router, loss, backward (parameter-shift through the circuit), and optimizer step.
Uses DataPipeline and Wasmtime traces when available; STE is handled by TernaryWASMExpert.
"""

from __future__ import annotations

import logging
from typing import Any, List, Literal, Optional, cast

import torch

from ..data.pipeline import DataPipeline
from ..hardware.device import AcceleratorType, get_device
from ..model import QMiniWASM
from .repro import set_training_seed

logger = logging.getLogger(__name__)

_D_MODEL = 4096

# Hugging Face: when hf_num_samples is None, load a larger slice than mesh/corpus (still cap download size).
_HF_AUTO_SAMPLES_FLOOR = 8192
_HF_AUTO_SAMPLES_CEIL = 200_000
_HF_AUTO_SAMPLES_BATCH_MULT = 256

TrainingDataSource = Literal["mesh", "corpus", "hf_tabular"]


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
    seed: int | None = None,
    grad_clip_norm: float | None = None,
    lr_plateau_patience: int | None = None,
    lr_plateau_factor: float = 0.5,
    lr_plateau_min_lr: float = 1e-7,
    early_stop_patience: int | None = None,
) -> dict[str, Any]:
    """Run the training curriculum for QMiniWASM.

    Args:
        epochs: Number of training epochs.
        batch_size: Batch size for the dataloader.
        learning_rate: AdamW learning rate.
        accelerator: "cuda", "xpu", or "cpu"; if None, use env PREFER_XPU / PREFER_CUDA.
        device_index: Device index for cuda/xpu.
        quantum_backend: Quantum backend id (used when building router; default penny_lane).
        num_qubits: Number of qubits for QAOA (for backend registry).
        qaoa_layers: QAOA layers (for backend registry).
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
        seed: If set, seeds Python / NumPy / torch for reproducible runs (best-effort on GPU).
        grad_clip_norm: If set, clip global gradient norm after backward (AdamW stability).
        lr_plateau_patience: If > 0, use ReduceLROnPlateau on epoch mean MSE (``mode=min``).
        lr_plateau_factor: LR multiplicative factor when plateau triggers.
        lr_plateau_min_lr: Minimum LR for the scheduler.
        early_stop_patience: If > 0, stop after this many epochs without improvement on best epoch MSE.

    Returns:
        Dict with ``epochs_run``, ``final_loss``, ``metrics`` (sample counts, epoch losses, device, etc.).
    """
    if seed is not None:
        set_training_seed(int(seed))

    device = get_device(
        accelerator=cast(AcceleratorType | None, accelerator),
        device_index=device_index,
    )

    model = QMiniWASM(device=device)
    model.quantum_router.train()
    if hasattr(model, "ternary_expert"):
        model.ternary_expert.train()

    optimizer = torch.optim.AdamW(
        list(model.quantum_router.parameters()) + list(model.ternary_expert.parameters()),
        lr=learning_rate,
    )

    scheduler: Optional[torch.optim.lr_scheduler.ReduceLROnPlateau] = None
    if lr_plateau_patience is not None and lr_plateau_patience > 0:
        scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
            optimizer,
            mode="min",
            factor=lr_plateau_factor,
            patience=lr_plateau_patience,
            min_lr=lr_plateau_min_lr,
        )

    pipeline = DataPipeline()
    source = (training_data_source or "mesh").strip().lower()
    num_samples = max(1, batch_size * 4)

    corpus_seed = int(seed) if seed is not None else 42

    if source == "corpus":
        if not data_path:
            raise ValueError("data_path must point to a corpus manifest JSON when training_data_source=corpus")
        processed_data = pipeline.generate_training_data_from_corpus(
            data_path, num_samples=num_samples, seed=corpus_seed
        )
    elif source == "hf_tabular":
        if not data_path:
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
        processed_data = load_hf_tabular_samples(
            data_path,
            hf_n,
            split=hf_split,
            config_name=hf_dataset_config,
            text_fields=hf_text_fields,
        )
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
    last_n_batches = 0
    skipped_nonfinite_batches = 0
    best_mse = float("inf")
    epochs_without_improvement = 0
    epochs_completed = 0
    stopped_early = False

    for epoch in range(epochs):
        epoch_loss = 0.0
        n_batches = 0
        for i in range(0, len(processed_data), batch_size):
            batch = processed_data[i : i + batch_size]
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
            out = model.hybrid_inference(hidden_states)
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
                torch.nn.utils.clip_grad_norm_(
                    list(model.quantum_router.parameters()) + list(model.ternary_expert.parameters()),
                    grad_clip_norm,
                )
            optimizer.step()

            epoch_loss += loss.item()
            n_batches += 1

        last_n_batches = n_batches
        if n_batches > 0:
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
            logger.warning("epoch=%s produced zero batches; check batch_size and data length", epoch + 1)

    metrics: dict[str, Any] = {
        "training_data_source": source,
        "num_samples": len(processed_data),
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
    }
    if seed is not None:
        metrics["seed"] = int(seed)
    if grad_clip_norm is not None:
        metrics["grad_clip_norm"] = float(grad_clip_norm)
    if scheduler is not None and lr_plateau_patience is not None:
        metrics["lr_plateau_patience"] = int(lr_plateau_patience)
        metrics["lr_plateau_factor"] = float(lr_plateau_factor)
    if early_stop_patience is not None and early_stop_patience > 0:
        metrics["early_stop_patience"] = int(early_stop_patience)

    return {
        "epochs_run": epochs_completed,
        "final_loss": final_loss,
        "metrics": metrics,
    }
