"""Optional Hugging Face tabular loader for hybrid_inference MSE pretraining.

This path does not provide WASM linear-memory semantics; it encodes each row as a
fixed 4096-dim vector for reconstruction-style training (target == hidden).
Install: pip install datasets (see setup.py extras_require training).
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

import torch

from qminiwasm.wasm.memory_encode import encode_linear_memory


def load_hf_tabular_samples(
    dataset_id: str,
    num_samples: int,
    *,
    split: str = "train",
    config_name: Optional[str] = None,
) -> List[Dict[str, Any]]:
    try:
        from datasets import load_dataset  # type: ignore[import-untyped]
    except ImportError as e:
        raise ImportError(
            "Hugging Face tabular training requires the `datasets` package. "
            "Install with: pip install datasets"
        ) from e

    if config_name:
        ds = load_dataset(dataset_id, config_name, split=split)
    else:
        ds = load_dataset(dataset_id, split=split)

    samples: List[Dict[str, Any]] = []
    for i, row in enumerate(ds):
        if i >= num_samples:
            break
        row_dict = dict(row)
        blob = str(sorted(row_dict.items())).encode("utf-8", errors="replace")
        hidden = encode_linear_memory(blob[:16384], result_i32=i, first_arg=0)
        target = hidden.clone()
        samples.append(
            {
                "algorithm": "hf_tabular",
                "inputs": [i],
                "output": i,
                "hidden": hidden,
                "target": target,
                "wasm_memory": b"",
                "stack_snapshot": [],
                "execution_state": {"dataset_id": dataset_id, "row_index": i},
            }
        )
    return samples
