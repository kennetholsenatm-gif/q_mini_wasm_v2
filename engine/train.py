"""Training entrypoint: load config from env and run the training loop."""

from __future__ import annotations

from .config import EngineConfig
from qminiwasm.training.loop import run_training_loop


def main(config: EngineConfig | None = None) -> dict:
    """Run the training loop with the given or env-derived config."""
    if config is None:
        config = EngineConfig()
    mesh_algos = None
    if getattr(config, "mesh_algorithms", None):
        mesh_algos = [a.strip() for a in str(config.mesh_algorithms).split(",") if a.strip()]

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
    )
