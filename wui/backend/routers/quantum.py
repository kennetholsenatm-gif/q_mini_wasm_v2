"""Quantum backend and API credentials API."""

import os
from pathlib import Path
import re

from fastapi import APIRouter, HTTPException

from ..models import (
    QuantumBackendInfo,
    QuantumConfigResponse,
    QuantumConfigUpdate,
    QuantumVerifyResponse,
)

# In-memory: current backend id (no raw credentials stored in memory for safety)
_quantum_backend: str | None = None
# Which credential keys have been set (no values stored)
_credentials_configured: dict[str, bool] = {}

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


def _get_credentials_configured(backend: str) -> dict[str, bool]:
    """Return per-key booleans for required env vars of the current backend."""
    backends_by_id = {b.id: b for b in QUANTUM_BACKENDS}
    info = backends_by_id.get(backend)
    if not info or not info.required_env_vars:
        return {}
    return {k: _credentials_configured.get(k, False) for k in info.required_env_vars}


@router.get("/config", response_model=QuantumConfigResponse)
async def get_quantum_config() -> QuantumConfigResponse:
    """Return current backend id and non-secret config. No API keys."""
    from ..config import get_settings
    s = get_settings()
    backend = _quantum_backend if _quantum_backend is not None else s.quantum_backend
    sim = "default.qubit" if backend == "penny_lane" else None
    creds = _get_credentials_configured(backend)
    return QuantumConfigResponse(backend=backend, simulator_name=sim, credentials_configured=creds)


def _safe_backend_id_for_filename(backend: str) -> str | None:
    """Return a sanitized backend id safe for use in filenames, or None if invalid."""
    # Allow only simple tokens to avoid path traversal or special characters in filenames.
    if re.fullmatch(r"[A-Za-z0-9_-]+", backend):
        return backend
    return None


def _persist_credentials(backend: str, credentials: dict[str, str]) -> None:
    """Optionally write credentials to a file for the job. Never log values."""
    cred_dir = os.environ.get("WUI_CREDENTIALS_DIR")
    if not cred_dir or not credentials:
        return
    safe_backend = _safe_backend_id_for_filename(backend)
    if safe_backend is None:
        # Backend id is not safe to use as a filename component; skip persistence.
        return
    path = Path(cred_dir)
    path.mkdir(parents=True, exist_ok=True)
    filepath = path / f"{safe_backend}.env"
    lines = [f"{k}={v}" for k, v in credentials.items() if v]
    if lines:
        filepath.write_text("\n".join(lines), encoding="utf-8")
        try:
            filepath.chmod(0o600)
        except OSError:
            pass


@router.put("/config", response_model=QuantumConfigResponse)
async def put_quantum_config(body: QuantumConfigUpdate) -> QuantumConfigResponse:
    """Set quantum backend and persist credentials (e.g. to env or Vault). Do not log credentials."""
    valid = {b.id for b in QUANTUM_BACKENDS}
    if body.backend not in valid:
        raise HTTPException(status_code=400, detail=f"Unknown backend: {body.backend}")
    global _quantum_backend, _credentials_configured
    _quantum_backend = body.backend
    for k, v in (body.credentials or {}).items():
        if v and isinstance(v, str) and v.strip():
            _credentials_configured[k] = True
    _persist_credentials(body.backend, body.credentials or {})
    return QuantumConfigResponse(
        backend=body.backend,
        simulator_name="default.qubit" if body.backend == "penny_lane" else None,
        credentials_configured=_get_credentials_configured(body.backend),
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
        except ImportError:
            return QuantumVerifyResponse(
                ok=False,
                message="PennyLane not installed in this environment. Configure PennyLane for the training job; the engine will use it at runtime.",
            )
        except Exception as e:
            return QuantumVerifyResponse(ok=False, message=str(e))
    if backend == "ibm_quantum":
        return QuantumVerifyResponse(ok=False, message="IBM Quantum verification not implemented")
    if backend == "intel_qs":
        return QuantumVerifyResponse(ok=False, message="Intel QS verification not implemented")
    return QuantumVerifyResponse(ok=False, message=f"Unknown backend: {backend}")
