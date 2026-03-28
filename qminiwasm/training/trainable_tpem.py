"""Shim: re-exports :mod:`qminiwasm.tpem.trainable_tpem` (canonical implementation)."""

from __future__ import annotations

from qminiwasm.tpem.trainable_tpem import (
    CHECKPOINT_FORMAT_VERSION,
    D_MODEL,
    TPEM_INTERCHANGE_MAGIC,
    TRAINABLE_TPEM_FORMAT_VERSION,
    TRAINABLE_TPEM_INTERCHANGE_VERSION,
    WLES_TRAINING_SIDE_VERSION,
    build_checkpoint_payload,
    build_trainable_tpem_payload,
    load_cascade_policy_from_checkpoint,
    load_cascade_policy_from_trainable_tpem,
    load_checkpoint_into_model,
    load_trainable_tpem_into_model,
    peek_trainable_tpem_geometry,
    save_checkpoint,
    save_trainable_tpem_artifact,
    save_trainable_tpem_interchange_v2,
)

__all__ = [
    "CHECKPOINT_FORMAT_VERSION",
    "D_MODEL",
    "TPEM_INTERCHANGE_MAGIC",
    "TRAINABLE_TPEM_FORMAT_VERSION",
    "TRAINABLE_TPEM_INTERCHANGE_VERSION",
    "WLES_TRAINING_SIDE_VERSION",
    "build_checkpoint_payload",
    "build_trainable_tpem_payload",
    "load_cascade_policy_from_checkpoint",
    "load_cascade_policy_from_trainable_tpem",
    "load_checkpoint_into_model",
    "load_trainable_tpem_into_model",
    "peek_trainable_tpem_geometry",
    "save_checkpoint",
    "save_trainable_tpem_artifact",
    "save_trainable_tpem_interchange_v2",
]
