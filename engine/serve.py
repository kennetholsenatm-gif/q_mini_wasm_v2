"""Minimal inference API for the serving container.

Loads QMiniWASM and exposes POST /infer and GET /health.
Run: uvicorn engine.serve:app --host 0.0.0.0 --port 8001

Optional: set ``QMINIWASM_CHECKPOINT`` to a file saved during training (same format as
``CHECKPOINT_SAVE_PATH`` / ``CHECKPOINT_BEST_PATH``). Or set ``QMINIWASM_SERVE_CONFIG`` to a TOML
file (see ``configs/serve/default.toml``) with a ``[serve]`` table; explicit table values override
defaults, and any omitted key falls back to the same environment variables as before.

If the checkpoint contains ``hybrid_adapter`` weights, the model **constructs** that submodule on
load (no need to set ``HYBRID_ADAPTER``). ``cascade_policy`` in the checkpoint loads into
``cascade_router`` when ``USE_CASCADE_ROUTER=1`` and shapes match training.

Device follows ``ACCELERATOR`` / ``get_device()``; use ``ACCELERATOR=cpu`` in containers for
deterministic behavior.
"""

from __future__ import annotations

import os
from typing import Any, List, Optional

import torch

from ._dotenv import load_dotenv_if_available

load_dotenv_if_available()
from fastapi import FastAPI, HTTPException  # noqa: E402
from pydantic import BaseModel, Field  # noqa: E402

app = FastAPI(title="QMiniWASM Inference", version="0.1.0")

_model: Optional[Any] = None


def _env_bool(name: str, default: bool = False) -> bool:
    v = os.environ.get(name, "").strip().lower()
    if not v:
        return default
    return v in ("1", "true", "yes", "on")


def _env_int(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if raw.isdigit() or (raw.startswith("-") and raw[1:].isdigit()):
        return int(raw)
    return default


def _pick_bool(env_name: str, file_val: Optional[bool], default: bool = False) -> bool:
    if file_val is not None:
        return bool(file_val)
    return _env_bool(env_name, default)


def _pick_int(env_name: str, file_val: Optional[int], default: int) -> int:
    if file_val is not None:
        return int(file_val)
    return _env_int(env_name, default)


def _serve_file_section():
    raw = os.environ.get("QMINIWASM_SERVE_CONFIG", "").strip()
    if not raw:
        return None
    from .training_schema import load_serve_toml

    return load_serve_toml(raw)


def get_model():
    global _model
    if _model is None:
        from qminiwasm.hardware.device import get_device
        from qminiwasm.model import QMiniWASM

        dev = get_device()
        s = _serve_file_section()
        use_adapt = _pick_bool("HYBRID_ADAPTER", s.hybrid_adapter if s else None, False)
        adapt_h = _pick_int("HYBRID_ADAPTER_HIDDEN", s.hybrid_adapter_hidden if s else None, 1024)
        use_cr = _pick_bool("USE_CASCADE_ROUTER", s.use_cascade_router if s else None, False)
        csd = _pick_int("CASCADE_STATE_DIM", s.cascade_state_dim if s else None, 8)
        cna = _pick_int("CASCADE_NUM_ACTIONS", s.cascade_num_actions if s else None, 4)
        crh = _pick_int("CASCADE_ROUTER_HIDDEN", s.cascade_router_hidden if s else None, 32)
        _model = QMiniWASM(
            device=dev,
            use_hybrid_adapter=use_adapt,
            hybrid_adapter_hidden=adapt_h,
            use_cascade_router=use_cr,
            cascade_state_dim=csd,
            cascade_num_actions=cna,
            cascade_router_hidden=crh,
        )
        ckpt = ""
        if s is not None and s.checkpoint:
            ckpt = str(s.checkpoint).strip()
        if not ckpt:
            ckpt = (
                os.environ.get("QMINIWASM_CHECKPOINT", "").strip()
                or os.environ.get("CHECKPOINT_LOAD_PATH", "").strip()
            )
        if ckpt:
            _model.load_trainable_checkpoint(ckpt, map_location=dev)
    return _model


class InferRequest(BaseModel):
    """Input for /infer. hidden_states: list of vectors (batch_size, d_model=4096)."""

    hidden_states: List[List[float]]


class InferResponse(BaseModel):
    output: List[List[float]]
    cascade_logits: Optional[List[List[float]]] = Field(
        default=None,
        description=(
            "Per-row router logits when USE_CASCADE_ROUTER=1 and checkpoint contains weights."
        ),
    )


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.post("/infer", response_model=InferResponse)
def infer(req: InferRequest) -> InferResponse:
    if not req.hidden_states:
        raise HTTPException(status_code=400, detail="hidden_states must be non-empty")
    try:
        model = get_model()
        tensor = torch.tensor(req.hidden_states, dtype=torch.float32)
        cascade_logits: Optional[List[List[float]]] = None
        with torch.no_grad():
            out = model.hybrid_inference(tensor)
            if getattr(model, "cascade_router", None) is not None:
                cl = model.cascade_logits_rows(tensor)
                if cl is not None:
                    cascade_logits = cl.cpu().tolist()
        return InferResponse(output=out.cpu().tolist(), cascade_logits=cascade_logits)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
