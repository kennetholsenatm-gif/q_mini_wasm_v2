"""PyTorch training loop for the classical ML components with quantum routing.

Orchestrates device selection (CUDA/ARC/CPU), model forward through the quantum
router, loss, backward (parameter-shift through the circuit), and optimizer step.
Uses DataPipeline and Wasmtime traces when available; STE is handled by TernaryWASMExpert.
"""

from __future__ import annotations

from typing import Any, List, Literal, cast

import torch

from ..data.pipeline import DataPipeline
from ..hardware.device import AcceleratorType, get_device
from ..model import QMiniWASM

TrainingDataSource = Literal["mesh", "corpus", "hf_tabular"]


def run_training_loop(
    epochs: int = 10,
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

    Returns:
        Dict with "epochs_run", "final_loss", "metrics" (placeholder).
    """
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

    pipeline = DataPipeline()
    source = (training_data_source or "mesh").strip().lower()
    num_samples = max(1, batch_size * 4)

    if source == "corpus":
        if not data_path:
            raise ValueError("data_path must point to a corpus manifest JSON when training_data_source=corpus")
        processed_data = pipeline.generate_training_data_from_corpus(
            data_path, num_samples=num_samples, seed=42
        )
    elif source == "hf_tabular":
        if not data_path:
            raise ValueError(
                "data_path must be a Hugging Face dataset id when training_data_source=hf_tabular"
            )
        from .hf_loader import load_hf_tabular_samples

        processed_data = load_hf_tabular_samples(
            data_path,
            num_samples,
            config_name=hf_dataset_config,
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

    if not processed_data:
        processed_data = [
            {"hidden": torch.randn(4096), "target": torch.randn(4096)}
            for _ in range(batch_size * 2)
        ]

    final_loss = 0.0
    for epoch in range(epochs):
        epoch_loss = 0.0
        n_batches = 0
        for i in range(0, len(processed_data), batch_size):
            batch = processed_data[i : i + batch_size]
            if not batch:
                continue
            hidden_list = [b.get("hidden", torch.randn(4096)) for b in batch]
            target_list = [b.get("target", torch.randn(4096)) for b in batch]
            hidden_states = torch.stack(hidden_list).to(device)
            targets = torch.stack(target_list).to(device)

            optimizer.zero_grad()
            out = model.hybrid_inference(hidden_states)
            loss = torch.nn.functional.mse_loss(out, targets)
            loss.backward()
            optimizer.step()

            epoch_loss += loss.item()
            n_batches += 1

        if n_batches > 0:
            final_loss = epoch_loss / n_batches

    return {
        "epochs_run": epochs,
        "final_loss": final_loss,
        "metrics": {},
    }
