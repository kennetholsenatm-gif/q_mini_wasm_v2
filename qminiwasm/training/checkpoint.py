"""Compatibility imports; implementation is :mod:`qminiwasm.tpem.trainable_tpem`."""

from __future__ import annotations

from qminiwasm.tpem.trainable_tpem import (
    CHECKPOINT_FORMAT_VERSION,
    D_MODEL,
    TRAINABLE_TPEM_FORMAT_VERSION,
    WLES_TRAINING_SIDE_VERSION,
    build_checkpoint_payload,
    build_trainable_tpem_payload,
    load_cascade_policy_from_checkpoint,
    load_cascade_policy_from_trainable_tpem,
    load_checkpoint_into_model,
    load_trainable_tpem_into_model,
    save_checkpoint,
    save_trainable_tpem_artifact,
)

__all__ = [
    "CHECKPOINT_FORMAT_VERSION",
    "D_MODEL",
    "TRAINABLE_TPEM_FORMAT_VERSION",
    "WLES_TRAINING_SIDE_VERSION",
    "build_checkpoint_payload",
    "build_trainable_tpem_payload",
    "load_cascade_policy_from_checkpoint",
    "load_cascade_policy_from_trainable_tpem",
    "load_checkpoint_into_model",
    "load_trainable_tpem_into_model",
    "save_checkpoint",
    "save_trainable_tpem_artifact",
]
