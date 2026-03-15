"""Circuit (PQC) selection API."""

from fastapi import APIRouter, HTTPException

from ..models import (
    CircuitCurrentResponse,
    CircuitCurrentUpdate,
    CircuitPreset,
)

# In-memory current circuit config
_circuit: dict | None = None

DEFAULT_PRESETS = [
    CircuitPreset(num_qubits=8, qaoa_layers=3, diff_method="parameter-shift"),
    CircuitPreset(num_qubits=8, qaoa_layers=5, diff_method="parameter-shift"),
    CircuitPreset(num_qubits=8, qaoa_layers=3, diff_method="finite-diff"),
]

router = APIRouter(prefix="/api/circuits", tags=["circuits"])


@router.get("/presets", response_model=list[CircuitPreset])
async def get_circuit_presets() -> list[CircuitPreset]:
    """Return presets for QAOA router (num_qubits, qaoa_layers, diff_method)."""
    return DEFAULT_PRESETS


@router.get("/current", response_model=CircuitCurrentResponse)
async def get_circuit_current() -> CircuitCurrentResponse:
    """Return current circuit config."""
    from ..config import get_settings

    s = get_settings()
    if _circuit:
        return CircuitCurrentResponse(**_circuit)
    return CircuitCurrentResponse(
        num_qubits=s.num_qubits,
        qaoa_layers=s.qaoa_layers,
        diff_method=s.diff_method,
    )


@router.put("/current", response_model=CircuitCurrentResponse)
async def put_circuit_current(body: CircuitCurrentUpdate) -> CircuitCurrentResponse:
    """Set current circuit config. Used by ML engine when building quantum router."""
    global _circuit
    if body.diff_method not in ("parameter-shift", "finite-diff"):
        raise HTTPException(
            status_code=400, detail="diff_method must be parameter-shift or finite-diff"
        )
    _circuit = {
        "num_qubits": body.num_qubits,
        "qaoa_layers": body.qaoa_layers,
        "diff_method": body.diff_method,
    }
    return CircuitCurrentResponse(**_circuit)
