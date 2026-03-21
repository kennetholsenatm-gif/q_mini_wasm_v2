"""Script to generate dataset for hierarchical-inference-architecture"""

import logging
from qminiwasm.data.pipeline import DataPipeline
from pathlib import Path

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


def main():
    """Generate comprehensive dataset for mesh network edge nodes"""
    logger.info("Starting dataset generation for hierarchical-inference-architecture")

    # Initialize data pipeline
    pipeline = DataPipeline()

    # Define mesh network algorithms
    mesh_algorithms = ["hash", "encrypt", "network", "routing", "consensus"]

    # Generate normal training data
    logger.info("Generating normal training data...")
    normal_data = pipeline.generate_training_data(
        algorithms=mesh_algorithms, num_samples=10000  # 10K samples per algorithm
    )

    # Inject faults for robustness training
    logger.info("Injecting faults for robustness training...")
    fault_types = ["bit_flip", "stack_drop", "out_of_bounds", "network_corruption"]
    fault_injected_data = pipeline.inject_faults(normal_data, fault_types=fault_types)

    # Generate corrupted data for state recovery training
    logger.info("Generating corrupted data for state recovery...")
    corrupted_data = pipeline.inject_faults(
        normal_data, fault_types=["bit_flip", "stack_drop", "out_of_bounds"]
    )

    # Apply state recovery
    logger.info("Applying state recovery mechanisms...")
    recovered_data = pipeline.state_recovery(corrupted_data)

    # Generate WASM traces for Tier 2 ingestion
    logger.info("Generating WASM execution traces...")
    wasm_traces = pipeline.load_wasm_traces(num_traces=1000)

    # Create dataset structure
    dataset_dir = Path("datasets/mesh_agents")
    dataset_dir.mkdir(parents=True, exist_ok=True)

    # Save normal data
    normal_path = dataset_dir / "training/normal_operations.json"
    save_data(normal_path, normal_data)
    logger.info(f"Saved normal operations data to {normal_path}")

    # Save fault injected data
    fault_path = dataset_dir / "training/fault_injected.json"
    save_data(fault_path, fault_injected_data)
    logger.info(f"Saved fault injected data to {fault_path}")

    # Save state recovery data
    recovery_path = dataset_dir / "training/state_recovery.json"
    save_data(recovery_path, recovered_data)
    logger.info(f"Saved state recovery data to {recovery_path}")

    # Save WASM traces
    traces_path = dataset_dir / "validation/wasm_traces.json"
    save_data(traces_path, wasm_traces)
    logger.info(f"Saved WASM traces to {traces_path}")

    logger.info("Dataset generation completed successfully!")
    logger.info(f"Total dataset size: {get_dataset_size(dataset_dir)}")


def save_data(path: Path, data: list):
    """Save data to JSON file with compression"""
    import json
    import gzip

    with gzip.open(path, "wt", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def get_dataset_size(directory: Path) -> str:
    """Get human-readable size of dataset directory"""
    import os

    total_size = 0
    for dirpath, dirnames, filenames in os.walk(directory):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            total_size += os.path.getsize(fp)
    for unit in ["B", "KB", "MB", "GB"]:
        if total_size < 1024:
            return f"{total_size:.1f} {unit}"
        total_size /= 1024
    return f"{total_size:.1f} TB"


if __name__ == "__main__":
    main()
