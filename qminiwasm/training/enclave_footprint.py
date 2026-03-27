"""Best-effort trainable TPEM footprint (MiB) for Tier-1 enclave caps.

The estimate mirrors keys serialized in :func:`qminiwasm.tpem.trainable_tpem.build_trainable_tpem_payload`
(quantum_router, ternary_expert, optional hybrid_adapter / cascade_policy) and adds a small fudge for
``torch.save`` overhead vs raw tensor bytes.
"""

from __future__ import annotations

from typing import Any, Optional

import torch
import torch.nn as nn

from qminiwasm.tpem.trainable_tpem import build_trainable_tpem_payload

# Fudge beyond summed tensor storage (pickle/container overhead); kept intentionally modest.
_TPEM_SAVE_OVERHEAD_RATIO = 0.08


def _state_dict_tensor_bytes(sd: dict[str, Any]) -> int:
    total = 0
    for v in sd.values():
        if isinstance(v, torch.Tensor):
            total += int(v.numel()) * int(v.element_size())
    return total


def estimate_trainable_tpem_size_mb(
    model: Any,
    cascade_policy: Optional[nn.Module] = None,
) -> float:
    """Return estimated serialized trainable TPEM size in **mebibytes** (MiB)."""
    payload = build_trainable_tpem_payload(model, meta=None, cascade_policy=cascade_policy)
    raw = 0
    for key in ("quantum_router", "ternary_expert", "hybrid_adapter", "cascade_policy"):
        block = payload.get(key)
        if isinstance(block, dict):
            raw += _state_dict_tensor_bytes(block)
    mib = raw / (1024 * 1024)
    return float(mib * (1.0 + _TPEM_SAVE_OVERHEAD_RATIO))
