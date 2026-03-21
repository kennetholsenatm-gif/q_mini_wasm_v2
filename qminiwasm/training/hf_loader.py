"""Optional Hugging Face tabular loader for hybrid_inference MSE pretraining.

This path does not provide WASM linear-memory semantics; it encodes each row as a
fixed 4096-dim vector for reconstruction-style training (target == hidden).
Install: pip install datasets (see setup.py extras_require training).

Runbook (CodeSearchNet Python example, after ``pip install -e ".[training]"``):

- Set ``TRAINING_DATA_SOURCE=hf_tabular``, ``DATA_PATH=code-search-net/code_search_net``,
  ``HF_DATASET_CONFIG=python``, ``HF_SPLIT=train``, ``HF_NUM_SAMPLES=2000`` (optional).
- Richer early signal (same dataset): auto-detect prefers ``whole_func_string`` (doc + full function)
  when present, so the bytes seen by the fixed-width encoder carry more structure than body-only.
- Override explicitly: ``HF_TEXT_FIELDS=whole_func_string`` or
  ``HF_TEXT_FIELDS=func_code_string,func_documentation_string``.
- Repro / stability: ``SEED=42``, ``GRAD_CLIP_NORM=1.0`` (optional), ``LOG_LEVEL=INFO``.
- Scale: unset ``HF_NUM_SAMPLES`` uses a large auto slice (floor 8k, cap 200k, scales with ``BATCH_SIZE``);
  set ``HF_NUM_SAMPLES=50000`` (or ``EPOCHS=50``) for even longer runs.
- Plateau: ``hf_tabular`` defaults to ``LR_PLATEAU_PATIENCE=2`` (ReduceLROnPlateau); set ``LR_PLATEAU_PATIENCE=0`` to disable.
  Optional: ``EARLY_STOP_PATIENCE=6``, ``LR_PLATEAU_FACTOR=0.5``, ``LR_PLATEAU_MIN_LR=1e-7``.
- Run: ``python -m engine`` from the repo root (editable install or PYTHONPATH).
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

import torch

from qminiwasm.wasm.memory_encode import encode_linear_memory

# Fallback order when ``whole_func_string`` is missing (e.g. CodeSearchNet subset exports).
_CODE_TEXT_KEYS_FALLBACK = ("func_code_string", "func_documentation_string")


def _row_blob_from_code_fields(row: Dict[str, Any]) -> Optional[bytes]:
    """Prefer a single rich field so the encoder prefix is not duplicated body+whole."""
    whole = row.get("whole_func_string")
    if whole is not None and str(whole).strip():
        return str(whole).encode("utf-8", errors="replace")
    parts: List[str] = []
    for key in _CODE_TEXT_KEYS_FALLBACK:
        val = row.get(key)
        if val is not None and str(val).strip():
            parts.append(str(val))
    if not parts:
        return None
    return "\n\n".join(parts).encode("utf-8", errors="replace")


def row_to_encoded_blob(
    row_dict: Dict[str, Any],
    text_fields: Optional[List[str]] = None,
) -> bytes:
    """Build UTF-8 bytes for encode_linear_memory from a dataset row."""
    if text_fields is not None and len(text_fields) > 0:
        parts: List[str] = []
        for key in text_fields:
            if key not in row_dict:
                continue
            val = row_dict[key]
            if val is not None:
                parts.append(str(val))
        if parts:
            return "\n\n".join(parts).encode("utf-8", errors="replace")
        return b""

    blob_code = _row_blob_from_code_fields(row_dict)
    if blob_code is not None:
        return blob_code

    return str(sorted(row_dict.items())).encode("utf-8", errors="replace")


def load_hf_tabular_samples(
    dataset_id: str,
    num_samples: int,
    *,
    split: str = "train",
    config_name: Optional[str] = None,
    text_fields: Optional[List[str]] = None,
) -> List[Dict[str, Any]]:
    """Load up to ``num_samples`` rows from Hugging Face ``datasets`` and encode for training.

    Args:
        dataset_id: Hub dataset id (e.g. ``code-search-net/code_search_net``).
        num_samples: Maximum rows to ingest.
        split: Split name passed to ``load_dataset``.
        config_name: Optional dataset configuration (e.g. ``python`` for CodeSearchNet).
        text_fields: If set, join these row keys in order; if None, auto-detect CodeSearchNet-style
            fields then fall back to serializing the sorted row dict.
    """
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
        blob = row_to_encoded_blob(row_dict, text_fields=text_fields)
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
                "execution_state": {
                    "dataset_id": dataset_id,
                    "row_index": i,
                    "split": split,
                },
            }
        )
    return samples
