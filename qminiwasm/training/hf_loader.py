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
- Scale: unset ``HF_NUM_SAMPLES`` uses a large auto slice (floor 32k, cap 300k, scales with ``BATCH_SIZE``);
  set ``HF_NUM_SAMPLES=100000`` (or ``EPOCHS=50``) for even longer runs.
- **WASI-only rows:** set ``HF_WASI_SLICE_ONLY=1`` to stream the split and keep only rows whose encoded
  UTF-8 blob matches WASI/WebAssembly syscall markers (e.g. ``wasi_snapshot_preview``, ``wasm32-wasip``).
  Uses **streaming** ``load_dataset`` so you can target ``HF_NUM_SAMPLES=100000`` without materializing
  the full split. Optional ``HF_WASI_MAX_SCAN`` caps how many source rows to scan (default 12_000_000).
- Richness: ``encode_linear_memory`` only ingests the first ~4088 UTF-8 bytes of each row blob. Auto mode
  prepends a short ``[context]`` block (``language``, ``func_name``, ``repo``, ``path`` when present).
  Disable: ``HF_CONTEXT_FIELDS=0``; override keys: ``HF_CONTEXT_FIELDS=repo,path,language``.
- Plateau: ``hf_tabular`` defaults to ``LR_PLATEAU_PATIENCE=2`` (ReduceLROnPlateau); set ``LR_PLATEAU_PATIENCE=0`` to disable.
  Optional: ``EARLY_STOP_PATIENCE=6``, ``LR_PLATEAU_FACTOR=0.5``, ``LR_PLATEAU_MIN_LR=1e-7``.
- Auth (optional): set ``HUGGING_FACE_HUB_TOKEN`` or ``HF_TOKEN`` for higher Hub rate limits / gated datasets (never commit tokens; use ``.env`` — gitignored).
- Reproducible Hub snapshots: set ``HF_DATASET_REVISION`` (git ref / commit); defaults to ``main``.
- Run: ``python -m engine`` from the repo root (editable install or PYTHONPATH).
"""

from __future__ import annotations

import logging
import os
from typing import Any, Dict, List, Optional

import torch

from qminiwasm.wasm.memory_encode import BODY_SLOTS, encode_linear_memory

logger = logging.getLogger(__name__)

# Fallback order when ``whole_func_string`` is missing (e.g. CodeSearchNet subset exports).
_CODE_TEXT_KEYS_FALLBACK = ("func_code_string", "func_documentation_string")

# Labeled metadata before code so the ~4088-byte encoder window sees structure (CodeSearchNet-style rows).
_DEFAULT_CONTEXT_FIELD_KEYS = ("language", "func_name", "repo", "path")

# Reserve UTF-8 budget for [context] so most of BODY_SLOTS remains for code start.
_CONTEXT_PREFIX_MAX_BYTES = 900
_CONTEXT_VALUE_MAX_CHARS = 220


def _resolve_context_keys(
    text_fields_explicit: bool,
    context_fields: Optional[List[str]],
) -> Optional[tuple[str, ...]]:
    """Return keys to embed in [context], or None to skip."""
    if context_fields is not None and len(context_fields) == 0:
        return None
    if context_fields is not None:
        return tuple(context_fields)
    if text_fields_explicit:
        return None
    return tuple(_DEFAULT_CONTEXT_FIELD_KEYS)


def _context_prefix_bytes(row: Dict[str, Any], keys: tuple[str, ...]) -> bytes:
    lines: List[str] = []
    for k in keys:
        val = row.get(k)
        if val is None:
            continue
        s = str(val).strip()
        if not s:
            continue
        if len(s) > _CONTEXT_VALUE_MAX_CHARS:
            s = s[: _CONTEXT_VALUE_MAX_CHARS - 3] + "..."
        lines.append(f"{k}: {s}")
    if not lines:
        return b""
    text = "[context]\n" + "\n".join(lines) + "\n\n[code]\n"
    out = text.encode("utf-8", errors="replace")
    if len(out) > _CONTEXT_PREFIX_MAX_BYTES:
        out = out[: _CONTEXT_PREFIX_MAX_BYTES - 3] + b"..."
    return out


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
    context_fields: Optional[List[str]] = None,
) -> bytes:
    """Build UTF-8 bytes for encode_linear_memory from a dataset row.

    Only the first ``BODY_SLOTS`` (~4088) bytes affect the 4096-d embedding; a short ``[context]``
    block is prepended in auto mode so that window mixes metadata with the start of the code.
    """
    text_fields_explicit = text_fields is not None and len(text_fields) > 0
    ctx_keys = _resolve_context_keys(text_fields_explicit, context_fields)

    if text_fields_explicit:
        parts: List[str] = []
        for key in text_fields:
            if key not in row_dict:
                continue
            val = row_dict[key]
            if val is not None:
                parts.append(str(val))
        if parts:
            body = "\n\n".join(parts).encode("utf-8", errors="replace")
        else:
            body = b""
    else:
        blob_code = _row_blob_from_code_fields(row_dict)
        if blob_code is not None:
            body = blob_code
        else:
            body = str(sorted(row_dict.items())).encode("utf-8", errors="replace")

    if ctx_keys:
        prefix = _context_prefix_bytes(row_dict, ctx_keys)
        if prefix:
            return prefix + body
    return body


# Substrings (matched on lowercased blob) for WASI / wasm32-wasi mentions in source.
_WASI_MARKERS_LOWER = (
    b"wasi_snapshot_preview",
    b"wasi_unstable",
    b"wasi_preview",
    b"wasm32-wasip",
    b"wasm32-wasi",
    b"target_family=wasi",
    b"import wasi",
    b"use wasi",
    b"::wasi::",
    b'extern "wasi"',
    b"extern 'wasi'",
    b"wasi::cli",
    b"wasi:http",
    b"wasip1",
    b"wasip2",
)


def encoded_blob_references_wasi(blob: bytes) -> bool:
    """True if ``blob`` (UTF-8 code + optional context) likely references WASI APIs or targets."""
    if not blob:
        return False
    lower = blob.lower()
    return any(m in lower for m in _WASI_MARKERS_LOWER)


def load_hf_tabular_samples(
    dataset_id: str,
    num_samples: int,
    *,
    split: str = "train",
    config_name: Optional[str] = None,
    text_fields: Optional[List[str]] = None,
    context_fields: Optional[List[str]] = None,
    token: Optional[str] = None,
    wasi_slice_only: bool = False,
    max_scan_rows: Optional[int] = None,
) -> List[Dict[str, Any]]:
    """Load up to ``num_samples`` rows from Hugging Face ``datasets`` and encode for training.

    Args:
        dataset_id: Hub dataset id (e.g. ``code-search-net/code_search_net``).
        num_samples: Maximum rows to ingest.
        split: Split name passed to ``load_dataset``.
        config_name: Optional dataset configuration (e.g. ``python`` for CodeSearchNet).
        text_fields: If set, join these row keys in order; if None, auto-detect CodeSearchNet-style
            fields then fall back to serializing the sorted row dict.
        context_fields: Optional list of row keys to prepend as ``[context]`` (see module doc). None
            uses defaults in auto text mode only; ``[]`` disables.
        token: Optional Hugging Face Hub token (same as ``datasets.load_dataset(..., token=...)``).
        wasi_slice_only: If True, stream the split and keep only rows that pass
            :func:`encoded_blob_references_wasi` (see env ``HF_WASI_SLICE_ONLY``).
        max_scan_rows: Stop scanning the split after this many source rows (exhaustion or cap).
            None means a large default when ``wasi_slice_only`` is True.
    """
    try:
        from datasets import load_dataset  # type: ignore[import-untyped]
    except ImportError as e:
        raise ImportError(
            "Hugging Face tabular training requires the `datasets` package. "
            "Install with: pip install datasets"
        ) from e

    load_kw: Dict[str, Any] = {}
    if token:
        load_kw["token"] = token
    hub_revision = os.environ.get("HF_DATASET_REVISION", "main").strip() or "main"

    use_streaming = bool(wasi_slice_only)
    if config_name:
        ds = load_dataset(
            dataset_id,
            config_name,
            split=split,
            streaming=use_streaming,
            revision=hub_revision,
            **load_kw,
        )
    else:
        ds = load_dataset(
            dataset_id,
            split=split,
            streaming=use_streaming,
            revision=hub_revision,
            **load_kw,
        )

    samples: List[Dict[str, Any]] = []
    if wasi_slice_only:
        cap = max_scan_rows if max_scan_rows is not None else 12_000_000
        scanned = 0
        accepted = 0
        for row in ds:
            scanned += 1
            if scanned > cap:
                logger.warning(
                    "HF WASI slice: hit max_scan_rows=%s after %s accepted (wanted %s).",
                    cap,
                    accepted,
                    num_samples,
                )
                break
            row_dict = dict(row)
            blob = row_to_encoded_blob(
                row_dict,
                text_fields=text_fields,
                context_fields=context_fields,
            )
            if not encoded_blob_references_wasi(blob):
                continue
            hidden = encode_linear_memory(blob[:BODY_SLOTS], result_i32=accepted, first_arg=0)
            target = hidden.clone()
            samples.append(
                {
                    "algorithm": "hf_tabular_wasi",
                    "inputs": [accepted],
                    "output": accepted,
                    "hidden": hidden,
                    "target": target,
                    "wasm_memory": b"",
                    "stack_snapshot": [],
                    "execution_state": {
                        "dataset_id": dataset_id,
                        "row_index": accepted,
                        "split": split,
                        "hf_scan_index": scanned,
                        "wasi_slice_only": True,
                    },
                }
            )
            accepted += 1
            if accepted >= num_samples:
                break
        if accepted < num_samples:
            logger.warning(
                "HF WASI slice: only collected %s/%s samples after scanning %s rows (cap=%s).",
                accepted,
                num_samples,
                scanned,
                cap,
            )
        else:
            logger.info(
                "HF WASI slice: collected %s samples (scanned %s rows, cap=%s).",
                accepted,
                scanned,
                cap,
            )
        return samples

    for i, row in enumerate(ds):
        if i >= num_samples:
            break
        row_dict = dict(row)
        blob = row_to_encoded_blob(
            row_dict,
            text_fields=text_fields,
            context_fields=context_fields,
        )
        hidden = encode_linear_memory(blob[:BODY_SLOTS], result_i32=i, first_arg=0)
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
