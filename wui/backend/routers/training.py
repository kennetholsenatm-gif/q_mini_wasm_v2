"""Training estimate and related API."""

from fastapi import APIRouter, Query

from ..models import TrainingEstimateResponse

router = APIRouter(prefix="/api/training", tags=["training"])


def _format_duration(seconds: float) -> str:
    if seconds < 60:
        return f"~{int(seconds)} sec"
    if seconds < 3600:
        return f"~{int(seconds / 60)} min"
    h = int(seconds / 3600)
    m = int((seconds % 3600) / 60)
    if m:
        return f"~{h} h {m} min"
    return f"~{h} h"


@router.get("/estimate", response_model=TrainingEstimateResponse)
async def get_training_estimate(
    accelerator: str | None = Query(None),
    quantum_backend: str | None = Query(None),
    num_qubits: int | None = Query(None),
    qaoa_layers: int | None = Query(None),
    epochs: int | None = Query(None),
    batch_size: int | None = Query(None),
) -> TrainingEstimateResponse:
    """Estimate training duration from current or provided config. Heuristic only."""
    from ..config import get_settings
    from . import hardware as hw_router
    from . import quantum as q_router
    from . import circuits as circ_router

    s = get_settings()
    num_samples_default = 1024

    if accelerator is None:
        hw = hw_router._current
        accelerator = hw.get("accelerator") or ("xpu" if s.prefer_xpu else ("cuda" if s.prefer_cuda else "cpu"))
    if quantum_backend is None:
        quantum_backend = q_router._quantum_backend or s.quantum_backend
    if num_qubits is None or qaoa_layers is None:
        circ = circ_router._circuit
        if circ:
            num_qubits = num_qubits if num_qubits is not None else circ["num_qubits"]
            qaoa_layers = qaoa_layers if qaoa_layers is not None else circ["qaoa_layers"]
        else:
            num_qubits = num_qubits or s.num_qubits
            qaoa_layers = qaoa_layers or s.qaoa_layers
    if epochs is None:
        epochs = 10
    if batch_size is None:
        batch_size = 32

    steps_per_epoch = max(1, num_samples_default // batch_size)
    steps = epochs * steps_per_epoch

    base_sec_per_step = 0.5
    if accelerator in ("cuda", "xpu"):
        acc_factor = 1.0
    else:
        acc_factor = 0.3
    if quantum_backend == "penny_lane":
        backend_factor = 1.0
    elif quantum_backend == "ibm_quantum":
        backend_factor = 3.0
    elif quantum_backend == "intel_qs":
        backend_factor = 2.0
    else:
        backend_factor = 1.0
    circuit_factor = 1.0 + 0.05 * (num_qubits - 8) + 0.02 * (qaoa_layers - 3)
    circuit_factor = max(0.5, circuit_factor)

    estimated_seconds = steps * base_sec_per_step * acc_factor * backend_factor * circuit_factor
    message = _format_duration(estimated_seconds)
    return TrainingEstimateResponse(estimated_seconds=estimated_seconds, message=message)
