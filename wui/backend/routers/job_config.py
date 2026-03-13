"""Aggregate job config (hardware + quantum + circuit) for the training job."""

from fastapi import APIRouter

from ..models import JobConfigResponse

router = APIRouter(prefix="/api", tags=["job"])


@router.get("/job-config", response_model=JobConfigResponse)
async def get_job_config() -> JobConfigResponse:
    """Return current WUI selections for the training job. No secrets."""
    from ..config import get_settings
    from . import hardware as hw_router
    from . import quantum as q_router
    from . import circuits as circ_router

    s = get_settings()
    hw = hw_router._current
    acc = hw.get("accelerator")
    if acc is None:
        acc = "xpu" if s.prefer_xpu else ("cuda" if s.prefer_cuda else "cpu")
    device_index = hw.get("device_index", s.device_index)
    quantum_backend = q_router._quantum_backend if q_router._quantum_backend is not None else s.quantum_backend
    circ = circ_router._circuit
    if circ:
        num_qubits = circ["num_qubits"]
        qaoa_layers = circ["qaoa_layers"]
        diff_method = circ["diff_method"]
    else:
        num_qubits = s.num_qubits
        qaoa_layers = s.qaoa_layers
        diff_method = s.diff_method

    return JobConfigResponse(
        accelerator=acc,
        device_index=device_index,
        quantum_backend=quantum_backend,
        num_qubits=num_qubits,
        qaoa_layers=qaoa_layers,
        diff_method=diff_method,
        epochs=10,
        batch_size=32,
    )
