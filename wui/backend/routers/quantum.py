"""Quantum backend and API credentials API."""

from fastapi import APIRouter, HTTPException

from ..models import (
    QuantumBackendInfo,
    QuantumConfigResponse,
    QuantumConfigUpdate,
    QuantumVerifyResponse,
)

# In-memory: current backend id (no raw credentials stored in memory for safety)
_quantum_backend: str | None = None

QUANTUM_BACKENDS = [
    QuantumBackendInfo(
        id="ibm_quantum",
        name="IBM Quantum",
        required_env_vars=["IBM_QUANTUM_API_KEY"],
    ),
    QuantumBackendInfo(
        id="intel_qs",
        name="Intel Quantum SDK / Intel QS",
        required_env_vars=["INTEL_QS_PYTHON", "INTEL_QUANTUM_SDK_PATH"],
    ),
    QuantumBackendInfo(
        id="penny_lane",
        name="PennyLane (default.qubit)",
        required_env_vars=[],
    ),
]

router = APIRouter(prefix="/api/quantum", tags=["quantum"])


@router.get("/backends", response_model=list[QuantumBackendInfo])
async def get_quantum_backends() -> list[QuantumBackendInfo]:
    """List available quantum backends and required env var names."""
    return QUANTUM_BACKENDS


@router.get("/config", response_model=QuantumConfigResponse)
async def get_quantum_config() -> QuantumConfigResponse:
    """Return current backend id and non-secret config. No API keys."""
    from ..config import get_settings
    s = get_settings()
    backend = _quantum_backend if _quantum_backend is not None else s.quantum_backend
    sim = "default.qubit" if backend == "penny_lane" else None
    return QuantumConfigResponse(backend=backend, simulator_name=sim)


@router.put("/config", response_model=QuantumConfigResponse)
async def put_quantum_config(body: QuantumConfigUpdate) -> QuantumConfigResponse:
    """Set quantum backend and persist credentials (e.g. to env or Vault). Do not log credentials."""
    valid = {b.id for b in QUANTUM_BACKENDS}
    if body.backend not in valid:
        raise HTTPException(status_code=400, detail=f"Unknown backend: {body.backend}")
    global _quantum_backend
    _quantum_backend = body.backend
    # In production: write body.credentials to Vault or inject into runtime env.
    # Here we do not store raw credentials in process memory.
    return QuantumConfigResponse(
        backend=body.backend,
        simulator_name="default.qubit" if body.backend == "penny_lane" else None,
    )


@router.post("/verify", response_model=QuantumVerifyResponse)
async def post_quantum_verify() -> QuantumVerifyResponse:
    """Test connection to the selected backend (e.g. PennyLane device creation)."""
    backend = _quantum_backend
    from ..config import get_settings
    if backend is None:
        backend = get_settings().quantum_backend
    if backend == "penny_lane":
        try:
            import pennylane as qml  # type: ignore
            dev = qml.device("default.qubit", wires=2)
            assert dev is not None
            return QuantumVerifyResponse(ok=True, message="PennyLane default.qubit OK")
        except ImportError as e:
            return QuantumVerifyResponse(ok=False, message=f"PennyLane not installed: {e}")
        except Exception as e:
            return QuantumVerifyResponse(ok=False, message=str(e))
    if backend == "ibm_quantum":
        return QuantumVerifyResponse(ok=False, message="IBM Quantum verification not implemented")
    if backend == "intel_qs":
        return QuantumVerifyResponse(ok=False, message="Intel QS verification not implemented")
    return QuantumVerifyResponse(ok=False, message=f"Unknown backend: {backend}")
