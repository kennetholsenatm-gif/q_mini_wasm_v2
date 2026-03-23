"""Qiskit execution for the same QAOA structure as :class:`NeuralQAOA` (no PennyLane device).

Supports:

- ``qiskit_statevector`` — :class:`qiskit.primitives.StatevectorEstimator` (exact, local).
- ``qiskit_ibm`` — IBM Quantum via :class:`qiskit_ibm_runtime.EstimatorV2` (real hardware; no
  autograd through the device).

Requires optional installs: ``pip install qiskit`` and for IBM ``pip install qiskit-ibm-runtime``.
"""

from __future__ import annotations

import logging
import os
from typing import Any, List, Optional

import numpy as np

logger = logging.getLogger(__name__)


def resolve_ibm_backend_name(
    explicit: Optional[str] = None,
    *,
    config_quantum_backend: Optional[str] = None,
) -> str:
    """Resolve IBM device name for Runtime Estimator.

    Order: explicit ``backend_name`` / ``QAOAConfig.ibm_backend_name``, ``IBM_BACKEND_NAME`` env,
    ``config_quantum_backend`` (``[hardware].quantum_backend`` / ``EngineConfig`` — often set in
    TOML without exporting env in the worker), then ``QUANTUM_BACKEND`` env when it is ``ibm_*``.
    """
    e = (explicit or "").strip()
    if e and e.lower() not in ("auto", "penny_lane"):
        return e
    ibm = os.environ.get("IBM_BACKEND_NAME", "").strip()
    if ibm and ibm.lower() not in ("auto", "penny_lane"):
        return ibm
    qb_cfg = (config_quantum_backend or "").strip()
    if qb_cfg and qb_cfg.lower() not in ("auto", "penny_lane") and qb_cfg.lower().startswith(
        "ibm_"
    ):
        return qb_cfg
    qb = os.environ.get("QUANTUM_BACKEND", "").strip()
    if qb and qb.lower() not in ("auto", "penny_lane") and qb.lower().startswith("ibm_"):
        return qb
    return ""


def _ibm_safe_rzz_angle(theta: float) -> float:
    """Map RZZ rotation angle into ``[0, π/2]`` for IBM Runtime native ``rzz``.

    :func:`qiskit_ibm_runtime.utils.validations.validate_rzz_pubs` rejects angles outside
    this range. Training uses signed :math:`\\gamma \\cdot \\text{coupling}` which can be
    negative; the IBM MoE path is gradient-detached, so we use ``|θ|`` and clip to the
    hardware window (large magnitudes are saturated at ``π/2``).
    """
    pi2 = np.pi / 2.0
    return float(np.clip(abs(float(theta)), 0.0, pi2))


def _pauli_z_observables(num_qubits: int) -> List[Any]:
    from qiskit.quantum_info import SparsePauliOp

    out: List[SparsePauliOp] = []
    for i in range(num_qubits):
        label = ["I"] * num_qubits
        label[i] = "Z"
        out.append(SparsePauliOp("".join(label)))
    return out


def build_qaoa_circuit_qiskit(
    num_qubits: int,
    num_layers: int,
    weights: np.ndarray,
    gamma: np.ndarray,
    beta: np.ndarray,
    bias: np.ndarray,
    coupling: np.ndarray,
    *,
    ibm_native_rzz: bool = False,
):
    """Match ``NeuralQAOA`` PennyLane ordering: H layer, then (problem + mixer) * num_layers.

    Args:
        ibm_native_rzz: If True, map each ``rzz`` angle into ``[0, π/2]`` so IBM Runtime
            Estimator accepts the circuit (see :func:`_ibm_safe_rzz_angle`). Statevector
            mode should use ``False`` to match unconstrained Qiskit semantics.
    """
    from qiskit import QuantumCircuit as QC

    qc = QC(num_qubits)
    for i in range(num_qubits):
        qc.h(i)
    for layer in range(num_layers):
        g = float(gamma[layer])
        for i in range(num_qubits):
            qc.rz(g * float(weights[i]) * float(bias[i]), i)
        for i in range(num_qubits):
            for j in range(i + 1, num_qubits):
                th = g * float(coupling[i, j])
                if ibm_native_rzz:
                    th = _ibm_safe_rzz_angle(th)
                qc.rzz(th, i, j)
        b = float(beta[layer])
        for i in range(num_qubits):
            qc.rx(b, i)
    return qc


def run_z_expectations_statevector(
    num_qubits: int,
    num_layers: int,
    weights: np.ndarray,
    gamma: np.ndarray,
    beta: np.ndarray,
    bias: np.ndarray,
    coupling: np.ndarray,
) -> np.ndarray:
    """Exact ⟨Z_i⟩ via statevector estimator (local, no IBM)."""
    from qiskit.primitives import StatevectorEstimator

    qc = build_qaoa_circuit_qiskit(
        num_qubits, num_layers, weights, gamma, beta, bias, coupling
    )
    obs = _pauli_z_observables(num_qubits)
    est = StatevectorEstimator()
    job = est.run([(qc, obs)])
    res = job.result()
    evs = np.asarray(res[0].data.evs, dtype=np.float64).flatten()
    if evs.size != num_qubits:
        raise RuntimeError(f"Expected {num_qubits} expectations, got {evs.size}")
    return evs


def run_z_expectations_ibm(
    num_qubits: int,
    num_layers: int,
    weights: np.ndarray,
    gamma: np.ndarray,
    beta: np.ndarray,
    bias: np.ndarray,
    coupling: np.ndarray,
    *,
    backend_name: Optional[str] = None,
    shots: int = 1024,
) -> np.ndarray:
    """⟨Z_i⟩ on IBM Quantum using Runtime EstimatorV2."""
    from qiskit_ibm_runtime import EstimatorV2, QiskitRuntimeService

    token = (
        os.environ.get("IBM_QUANTUM_API_TOKEN", "").strip()
        or os.environ.get("QISKIT_IBM_TOKEN", "").strip()
    )
    if not token:
        raise RuntimeError(
            "IBM_QUANTUM_API_TOKEN or QISKIT_IBM_TOKEN must be set for qiskit_ibm execution mode."
        )
    name = resolve_ibm_backend_name(backend_name)
    if not name:
        raise RuntimeError(
            "Set IBM_BACKEND_NAME, or [hardware].quantum_backend in the training TOML, or "
            "QUANTUM_BACKEND in the environment, to a real IBM device (e.g. ibm_torino) for "
            "qiskit_ibm mode."
        )

    qc = build_qaoa_circuit_qiskit(
        num_qubits,
        num_layers,
        weights,
        gamma,
        beta,
        bias,
        coupling,
        ibm_native_rzz=True,
    )
    obs = _pauli_z_observables(num_qubits)

    service = None
    for ch in ("ibm_quantum", "ibm_cloud", "ibm_quantum_platform"):
        try:
            service = QiskitRuntimeService(channel=ch, token=token)
            break
        except Exception:
            continue
    if service is None:
        service = QiskitRuntimeService(token=token)

    backend = service.backend(name)
    # IBM Runtime requires ISA circuits (native basis + routing) since March 2024; logical H/RX/RZZ
    # must be transpiled to the backend target before EstimatorV2.
    from qiskit.transpiler.preset_passmanagers import generate_preset_pass_manager

    pm = generate_preset_pass_manager(optimization_level=1, backend=backend)
    isa_circuit = pm.run(qc)
    if isa_circuit.layout is not None:
        obs_isa = [
            op.apply_layout(isa_circuit.layout, isa_circuit.num_qubits) for op in obs
        ]
    else:
        obs_isa = obs

    estimator = EstimatorV2(mode=backend)
    job = estimator.run([(isa_circuit, obs_isa)], precision=1.0 / max(1, int(shots)))
    result = job.result()
    evs = np.asarray(result[0].data.evs, dtype=np.float64).flatten()
    if evs.size != num_qubits:
        raise RuntimeError(f"IBM Estimator: expected {num_qubits} Z expectations, got {evs.size}")
    return evs


def run_z_expectations(
    mode: str,
    num_qubits: int,
    num_layers: int,
    weights: np.ndarray,
    gamma: np.ndarray,
    beta: np.ndarray,
    bias: np.ndarray,
    coupling: np.ndarray,
    *,
    ibm_backend_name: Optional[str] = None,
    ibm_shots: int = 1024,
) -> np.ndarray:
    if mode == "qiskit_statevector":
        return run_z_expectations_statevector(
            num_qubits, num_layers, weights, gamma, beta, bias, coupling
        )
    if mode == "qiskit_ibm":
        return run_z_expectations_ibm(
            num_qubits,
            num_layers,
            weights,
            gamma,
            beta,
            bias,
            coupling,
            backend_name=ibm_backend_name,
            shots=ibm_shots,
        )
    raise ValueError(f"Unknown qiskit execution mode: {mode!r}")
