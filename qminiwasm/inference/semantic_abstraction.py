"""Semantic abstraction before Fog/Cloud escalation (compressed context, not raw history)."""

from __future__ import annotations

import hashlib
from typing import Any, Dict, Optional

import torch


def _digest_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:32]


def build_semantic_blob(
    *,
    hidden_preview: Optional[torch.Tensor] = None,
    kv_window_len: int = 0,
    relation_hints: Optional[Dict[str, Any]] = None,
    raw_prompt_digest: Optional[bytes] = None,
) -> Dict[str, Any]:
    """Build a small JSON-serializable semantic payload for tier handoff."""
    blob: Dict[str, Any] = {
        "schema_version": 1,
        "kv_window_len": int(kv_window_len),
    }
    if hidden_preview is not None:
        with torch.no_grad():
            h = hidden_preview.detach().float().flatten()[:64].cpu()
            blob["hidden_prefix_dim"] = int(h.numel())
            blob["hidden_prefix_sum"] = float(h.sum().item())
            blob["hidden_prefix_norm"] = float(h.norm().item())
    if relation_hints:
        blob["relation_hints"] = {str(k): str(v) for k, v in list(relation_hints.items())[:32]}
    if raw_prompt_digest is not None:
        blob["raw_prompt_digest"] = _digest_bytes(raw_prompt_digest)
    return blob


def attach_semantic_blob_to_state(state: Dict[str, Any], device: torch.device) -> None:
    """Mutate ``state`` with ``semantic_blob`` derived from execution_state / result."""
    es = state.get("execution_state") if isinstance(state.get("execution_state"), dict) else {}
    hs = es.get("hidden_state") if isinstance(es, dict) else None
    preview: Optional[torch.Tensor] = None
    if hs is not None and isinstance(hs, torch.Tensor):
        preview = hs.to(device=device)
    elif isinstance(state.get("result"), torch.Tensor):
        preview = state["result"].flatten()[:64].to(device=device)
    mem = es.get("wasm_memory_post") if isinstance(es, dict) else None
    if mem is None:
        mem = es.get("linear_memory") if isinstance(es, dict) else None
    raw_digest: Optional[bytes] = None
    if isinstance(mem, (bytes, bytearray)):
        raw_digest = bytes(mem[: min(512, len(mem))])
    blob = build_semantic_blob(
        hidden_preview=preview,
        kv_window_len=int(es.get("kv_window_len", 0)) if isinstance(es, dict) else 0,
        relation_hints=es.get("relation_hints") if isinstance(es, dict) else None,
        raw_prompt_digest=raw_digest,
    )
    state["semantic_blob"] = blob
