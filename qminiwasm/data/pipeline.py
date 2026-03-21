"""Data Pipeline Implementation

This module implements the Synthetic Data Pipeline (Pillar 5) which provides the data infrastructure
for training the Q-Mini-WASM architecture. It includes Wasmtime instrumentation, fault injection,
and state recovery mechanisms.

The implementation follows the white paper's specifications for:
- Wasmtime instrumentation and stack/memory delta capture
- Intentional fault injection for robustness training
- State recovery using HullKVCache
- Continuous pre-training and CISPO integration
"""

import hashlib
import json
import logging
import random
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

import torch

from qminiwasm.wasm import MESH_EXPORT_NAMES, WasmCompiler, WasmEngine, MESH_ALGORITHMS

BytesLike = Union[bytes, bytearray, memoryview]


@dataclass
class delta_payload_struct:
    """Delta payload with metadata for attestation and streaming."""

    format_version: int
    length: int
    checksum: str
    deltas: List[Tuple[int, Optional[bytes]]]


class HullKVCache:
    """Mock HullKVCache for state recovery mechanism"""

    def __init__(self):
        self.cache = {}
        self.logger = logging.getLogger(__name__)

    def store_valid_state(self, hidden_state: torch.Tensor, target_state: torch.Tensor):
        """Store a valid state transition in the cache"""
        state_key = tuple(hidden_state.tolist())
        self.cache[state_key] = target_state.clone()

    def retrieve_target_state(self, hidden_state: torch.Tensor) -> Optional[torch.Tensor]:
        """Retrieve the target state for a given hidden state"""
        state_key = tuple(hidden_state.tolist())
        return self.cache.get(state_key, None)

    def generate_corrective_tokens(
        self, corrupted_state: torch.Tensor, valid_state: torch.Tensor
    ) -> torch.Tensor:
        """Generate corrective tokens to transform corrupted state to valid state"""
        # Calculate the difference between corrupted and valid states
        difference = valid_state - corrupted_state
        # Create corrective tokens (simple delta encoding)
        corrective_tokens = torch.clamp(difference, -10, 10)  # Limit correction magnitude
        return corrective_tokens


class DataPipeline:
    """DataPipeline: Synthetic Data Pipeline for Q-Mini-WASM Training

    This class implements the Synthetic Data Pipeline (Pillar 5) which provides:
    - Wasmtime instrumentation for training data generation
    - Fault injection for robustness training
    - State recovery mechanisms
    - Continuous pre-training curriculum

    The implementation follows the white paper's specifications for:
    - Wasmtime instrumentation and stack/memory delta capture
    - Intentional fault injection for robustness training
    - State recovery using HullKVCache
    - Continuous pre-training and CISPO integration
    """

    def __init__(self):
        """Initialize the DataPipeline."""
        self.logger = logging.getLogger(__name__)
        self.logger.setLevel(logging.INFO)
        self.logger.info("Initialized DataPipeline")
        self.wasm_engine = WasmEngine(use_mock=False)  # Use actual WASM compilation
        self.wasm_compiler = WasmCompiler(self.wasm_engine)
        self.hullkv_cache = HullKVCache()
        self._wasm_cache = {}

    def generate_training_data(self, algorithms: List[str], num_samples: int) -> List[Dict]:
        """Generate training data using Wasmtime instrumentation.

        This method implements the training data generation described in the white paper:
        - Compiles C algorithms to WASM
        - Instruments execution for stack/memory delta capture
        - Generates token sequences for training

        Args:
            algorithms: List of algorithm names to generate
            num_samples: Number of samples to generate

        Returns:
            List of training data samples with "hidden" and "target" torch.Tensor keys.
        """
        self.logger.info("Generating training data for %d algorithms", len(algorithms))
        training_data = []

        # Predefined algorithms for mesh network operations
        mesh_algorithms = ["hash", "encrypt", "network", "routing", "consensus"]

        for algo_name in algorithms:
            if algo_name not in mesh_algorithms:
                self.logger.warning("Unknown algorithm: %s", algo_name)
                continue

            # Get or compile the WASM module for this algorithm
            if algo_name in self._wasm_cache:
                wasm_module = self._wasm_cache[algo_name]
            else:
                c_code = MESH_ALGORITHMS[algo_name]
                wasm_module = self.wasm_compiler.compile_algorithm(algo_name, c_code)
                if wasm_module:
                    self._wasm_cache[algo_name] = wasm_module

            if wasm_module is None:
                self.logger.error("Failed to compile %s. Using mock execution.", algo_name)
                # Fallback to mock execution if compilation fails
                for _ in range(num_samples):
                    inputs = [random.randint(0, 1000), random.randint(0, 1000)]
                    (
                        output,
                        hidden_state,
                        target_state,
                        pre_mem,
                        post_mem,
                    ) = self.wasm_engine._execute_mock(algo_name, inputs)
                    self.hullkv_cache.store_valid_state(hidden_state, target_state)
                    sample = {
                        "algorithm": algo_name,
                        "inputs": inputs,
                        "output": output,
                        "hidden": hidden_state,
                        "target": target_state,
                        "wasm_memory": post_mem,
                        "stack_snapshot": [output],
                        "execution_state": {
                            "instruction_pointer": 0,
                            "memory_size": hidden_state.numel(),
                        },
                    }
                    training_data.append(sample)
                continue

            export_name = MESH_EXPORT_NAMES.get(algo_name, algo_name)
            for _ in range(num_samples):
                # Generate random inputs (2 integers for simplicity)
                inputs = [random.randint(0, 1000), random.randint(0, 1000)]

                try:
                    (
                        output,
                        hidden_state,
                        target_state,
                        pre_mem,
                        post_mem,
                    ) = self.wasm_engine.execute_wasm(wasm_module, export_name, inputs)

                    if hidden_state is None or target_state is None:
                        (
                            output,
                            hidden_state,
                            target_state,
                            pre_mem,
                            post_mem,
                        ) = self.wasm_engine._execute_mock(algo_name, inputs)

                    # Store valid state transition in HullKVCache
                    self.hullkv_cache.store_valid_state(hidden_state, target_state)

                    # Create training sample with proper structure
                    sample = {
                        "algorithm": algo_name,
                        "inputs": inputs,
                        "output": output,
                        "hidden": hidden_state,
                        "target": target_state,
                        "wasm_memory": post_mem,
                        "stack_snapshot": [output],
                        "execution_state": {
                            "instruction_pointer": 0,
                            "memory_size": hidden_state.numel(),
                        },
                    }
                    training_data.append(sample)

                except Exception as e:
                    self.logger.error("Failed to generate data for %s: %s", algo_name, str(e))

        self.logger.info("Generated %d training samples", len(training_data))
        return training_data

    def generate_training_data_from_corpus(
        self,
        manifest_path: str,
        num_samples: int,
        seed: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        """Load bare wasm32 modules from a JSON manifest and emit training samples.

        Manifest schema (minimal):
            {
              "version": 1,
              "base_dir": "<optional, relative to manifest parent>",
              "entries": [
                {
                  "id": "optional",
                  "wasm_path": "relative/or/absolute/path.wasm",
                  "export_func": "run",
                  "num_args": 2,
                  "arg_max": 1000
                }
              ]
            }
        """
        path = Path(manifest_path).resolve()
        if not path.is_file():
            self.logger.error("Corpus manifest not found: %s", manifest_path)
            return []

        with path.open("r", encoding="utf-8") as f:
            manifest = json.load(f)

        manifest_base = path.parent
        if manifest.get("base_dir"):
            manifest_base = (manifest_base / str(manifest["base_dir"])).resolve()

        entries = manifest.get("entries") or []
        if not entries:
            self.logger.warning("Corpus manifest has no entries: %s", manifest_path)
            return []

        rng = random.Random(seed if seed is not None else 42)
        out: List[Dict[str, Any]] = []
        n_entries = len(entries)
        per_entry_base = num_samples // n_entries
        rem = num_samples % n_entries

        for i, entry in enumerate(entries):
            n_here = per_entry_base + (1 if i < rem else 0)
            if n_here <= 0:
                continue
            rel = entry.get("wasm_path")
            export_func = entry.get("export_func")
            if not rel or not export_func:
                self.logger.warning("Skipping corpus entry missing wasm_path/export_func: %s", entry)
                continue
            wasm_file = Path(rel)
            if not wasm_file.is_file():
                wasm_file = (manifest_base / rel).resolve()
            if not wasm_file.is_file():
                self.logger.error("WASM file not found for corpus: %s", rel)
                continue

            wasm_bytes = wasm_file.read_bytes()
            module = self.wasm_engine.compile_wasm(wasm_bytes)
            if module is None:
                self.logger.error("Failed to load WASM module: %s", wasm_file)
                continue

            num_args = int(entry.get("num_args", 2))
            arg_max = int(entry.get("arg_max", 1000))
            entry_id = entry.get("id", wasm_file.stem)

            for _ in range(n_here):
                inputs = [rng.randint(0, arg_max) for _ in range(num_args)]
                try:
                    (
                        output,
                        hidden_state,
                        target_state,
                        pre_mem,
                        post_mem,
                    ) = self.wasm_engine.execute_wasm(module, export_func, inputs)
                    if hidden_state is None or target_state is None:
                        continue
                    self.hullkv_cache.store_valid_state(hidden_state, target_state)
                    out.append(
                        {
                            "algorithm": str(entry_id),
                            "inputs": inputs,
                            "output": output,
                            "hidden": hidden_state,
                            "target": target_state,
                            "wasm_memory": post_mem,
                            "stack_snapshot": [output],
                            "execution_state": {
                                "instruction_pointer": 0,
                                "memory_size": hidden_state.numel(),
                                "wasm_path": str(wasm_file),
                            },
                        }
                    )
                except Exception as e:
                    self.logger.error("Corpus sample failed for %s: %s", wasm_file, e)

        self.logger.info("Generated %d corpus training samples", len(out))
        return out

    def inject_faults(self, training_data: List[Dict], fault_types: List[str]) -> List[Dict]:
        """Inject faults into training data for robustness.

        This method implements fault injection strategies described in the white paper:
        - Bit flipping in written integers
        - Stack item dropping
        - Out-of-bounds pointer calculations
        - State recovery token sequences

        Args:
            training_data: Training data to modify
            fault_types: Types of faults to inject

        Returns:
            Modified training data with injected faults
        """
        self.logger.info(f"Injecting {len(fault_types)} fault types")
        corrupted_data = []

        fault_probabilities = {
            "bit_flip": 0.1,  # 10% chance of bit flip
            "stack_drop": 0.05,  # 5% chance of stack drop
            "oob_pointer": 0.02,  # 2% chance of out-of-bounds pointer
        }

        for sample in training_data:
            corrupted_sample = sample.copy()

            # Apply each fault type probabilistically
            for fault_type in fault_types:
                if fault_type == "bit_flip" and random.random() < fault_probabilities["bit_flip"]:
                    h = corrupted_sample["hidden"]
                    bit_flip_mask = torch.bernoulli(torch.full_like(h, 0.01))
                    noise = torch.randn_like(h) * 0.1
                    corrupted_sample["hidden"] = h + bit_flip_mask * noise
                    corrupted_sample["faults"] = corrupted_sample.get("faults", []) + ["bit_flip"]

                elif (
                    fault_type == "stack_drop"
                    and random.random() < fault_probabilities["stack_drop"]
                ):
                    # Drop random elements from the hidden state
                    drop_mask = torch.bernoulli(torch.full_like(corrupted_sample["hidden"], 0.2))
                    corrupted_sample["hidden"] = corrupted_sample["hidden"] * (1 - drop_mask)
                    corrupted_sample["faults"] = corrupted_sample.get("faults", []) + ["stack_drop"]

                elif (
                    fault_type == "oob_pointer"
                    and random.random() < fault_probabilities["oob_pointer"]
                ):
                    # Create out-of-bounds pointer effect by adding large values
                    oob_mask = torch.bernoulli(torch.full_like(corrupted_sample["hidden"], 0.1))
                    corrupted_sample["hidden"] = corrupted_sample["hidden"] + oob_mask * 1000
                    corrupted_sample["faults"] = corrupted_sample.get("faults", []) + [
                        "oob_pointer"
                    ]

            corrupted_data.append(corrupted_sample)

        return corrupted_data

    def state_recovery(self, corrupted_data: List[Dict]) -> List[Dict]:
        """Implement state recovery for corrupted data.

        This method implements state recovery mechanisms described in the white paper:
        - Uses HullKVCache to retrieve last valid state
        - Generates corrective tokens
        - Reverts pointer arithmetic or fixes stack alignment

        Args:
            corrupted_data: Corrupted training data

        Returns:
            Recovered training data
        """
        self.logger.info("Executing state recovery")
        recovered_data = []

        for sample in corrupted_data:
            recovered_sample = sample.copy()

            # Check if the sample has faults
            if "faults" in sample:
                # Try to recover from bit flip
                if "bit_flip" in sample["faults"]:
                    # Use HullKVCache to get the valid target state
                    valid_target = self.hullkv_cache.retrieve_target_state(sample["hidden"])
                    if valid_target is not None:
                        # Generate corrective tokens
                        corrective_tokens = self.hullkv_cache.generate_corrective_tokens(
                            sample["hidden"], valid_target
                        )
                        # Apply correction
                        recovered_sample["hidden"] = sample["hidden"] + corrective_tokens
                        recovered_sample["recovery"] = recovered_sample.get("recovery", []) + [
                            "bit_flip_recovered"
                        ]

                # Try to recover from stack drop
                if "stack_drop" in sample["faults"]:
                    # Fill dropped elements with mean value
                    mean_value = torch.mean(sample["hidden"])
                    recovered_sample["hidden"] = torch.where(
                        sample["hidden"] == 0, mean_value, sample["hidden"]
                    )
                    recovered_sample["recovery"] = recovered_sample.get("recovery", []) + [
                        "stack_recovered"
                    ]

                # Try to recover from out-of-bounds pointer
                if "oob_pointer" in sample["faults"]:
                    # Clamp out-of-bounds values to valid range
                    recovered_sample["hidden"] = torch.clamp(sample["hidden"], -1000, 1000)
                    recovered_sample["recovery"] = recovered_sample.get("recovery", []) + [
                        "oob_pointer_recovered"
                    ]

            recovered_data.append(recovered_sample)

        return recovered_data

    def load_wasm_traces(self, num_traces: int = 10) -> List[Dict[str, Any]]:
        """Load or generate WASM execution traces for Tier 2 ingestion and training."""
        traces: List[Dict[str, Any]] = []
        for _ in range(num_traces):
            # Generate synthetic trace with realistic values
            mem = torch.randint(0, 256, (64,), dtype=torch.uint8).numpy().tobytes()
            traces.append(
                {
                    "linear_memory": mem,
                    "stack_snapshot": [random.randint(0, 100) for _ in range(3)],
                    "instruction_pointer": random.randint(0, 100),
                    "algorithm": random.choice(
                        ["hash", "encrypt", "network", "routing", "consensus"]
                    ),
                }
            )
        self.logger.info("Loaded %d WASM traces", len(traces))
        return traces


def _to_bytes(mem: Any) -> Optional[bytes]:
    """Convert memory-like object to bytes"""
    if mem is None:
        return None
    if isinstance(mem, (bytes, bytearray)):
        return bytes(mem)
    if isinstance(mem, torch.Tensor):
        return mem.numpy().tobytes()
    if hasattr(mem, "tobytes"):
        return mem.tobytes()
    return None


def compress_deltas(
    current_memory: Any,
    baseline_memory: Any,
    format_version: int = 1,
    chunk_size: int = 8,
) -> delta_payload_struct:
    """Compute binary diff of current linear memory vs baseline (loop 0)."""
    cur = _to_bytes(current_memory)
    base = _to_bytes(baseline_memory)
    deltas: List[Tuple[int, Optional[bytes]]] = []

    if cur is None and base is None:
        pass
    elif cur is None:
        pass
    elif base is None or len(base) == 0:
        for addr in range(0, len(cur), chunk_size):
            end = min(addr + chunk_size, len(cur))
            chunk = cur[addr:end]
            if chunk:
                deltas.append((addr, chunk))
    else:
        max_len = max(len(cur), len(base))
        for addr in range(0, max_len, chunk_size):
            end_cur = min(addr + chunk_size, len(cur))
            end_base = min(addr + chunk_size, len(base))
            c = cur[addr:end_cur] if end_cur > addr else b""
            b = base[addr:end_base] if end_base > addr else b""
            if c != b:
                deltas.append((addr, c if c else None))

    # Serialize for checksum
    buf = struct.pack("<I", format_version)
    for addr, val in deltas:
        buf += struct.pack("<I", addr)
        if val is not None:
            buf += struct.pack("<I", len(val)) + val
        else:
            buf += struct.pack("<I", 0)
    checksum = hashlib.sha256(buf).hexdigest()

    return delta_payload_struct(
        format_version=format_version,
        length=len(buf),
        checksum=checksum,
        deltas=deltas,
    )
