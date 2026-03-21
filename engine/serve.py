"""Minimal inference API for the serving container.

Loads QMiniWASM and exposes POST /infer and GET /health.
Run: uvicorn engine.serve:app --host 0.0.0.0 --port 8001
"""

from __future__ import annotations

from typing import Any, List, Optional

import torch
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

app = FastAPI(title="QMiniWASM Inference", version="0.1.0")

_model: Optional[Any] = None


def get_model():
    global _model
    if _model is None:
        from qminiwasm.model import QMiniWASM

        _model = QMiniWASM(device=torch.device("cpu"))
    return _model


class InferRequest(BaseModel):
    """Input for /infer. hidden_states: list of vectors (batch_size, d_model=4096)."""

    hidden_states: List[List[float]]


class InferResponse(BaseModel):
    output: List[List[float]]


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
        with torch.no_grad():
            out = model.hybrid_inference(tensor)
        return InferResponse(output=out.cpu().tolist())
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
