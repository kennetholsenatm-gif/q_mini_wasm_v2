"""Smoke tests for Qiskit QAOA expectations (no IBM credentials)."""

import math

import numpy as np


def test_ibm_error_suggests_quota_or_capacity_fallback_heuristic():
    from qminiwasm.quantum.qiskit_qaoa import ibm_error_suggests_quota_or_capacity_fallback

    assert ibm_error_suggests_quota_or_capacity_fallback(
        RuntimeError(
            "This instance has met its usage limit. Workloads will not run until time is made available."
        )
    )
    assert not ibm_error_suggests_quota_or_capacity_fallback(
        RuntimeError("Set IBM_BACKEND_NAME to a real device")
    )


def test_ibm_safe_rzz_angle_maps_negative_and_clips():
    from qminiwasm.quantum.qiskit_qaoa import _ibm_safe_rzz_angle

    pi2 = np.pi / 2.0
    assert math.isclose(_ibm_safe_rzz_angle(-0.9272463446480117), 0.9272463446480117)
    assert _ibm_safe_rzz_angle(0.0) == 0.0
    assert math.isclose(_ibm_safe_rzz_angle(99.0), pi2)


def test_resolve_ibm_backend_name_prefers_explicit_then_ibm_env_then_quantum_backend(monkeypatch):
    from qminiwasm.quantum.qiskit_qaoa import resolve_ibm_backend_name

    monkeypatch.delenv("IBM_BACKEND_NAME", raising=False)
    monkeypatch.delenv("QUANTUM_BACKEND", raising=False)
    assert resolve_ibm_backend_name("ibm_torino") == "ibm_torino"

    monkeypatch.setenv("IBM_BACKEND_NAME", "ibm_osaka")
    monkeypatch.setenv("QUANTUM_BACKEND", "ibm_torino")
    assert resolve_ibm_backend_name(None) == "ibm_osaka"

    monkeypatch.delenv("IBM_BACKEND_NAME", raising=False)
    assert resolve_ibm_backend_name(None) == "ibm_torino"

    monkeypatch.setenv("QUANTUM_BACKEND", "penny_lane")
    assert resolve_ibm_backend_name(None) == ""

    monkeypatch.delenv("IBM_BACKEND_NAME", raising=False)
    monkeypatch.delenv("QUANTUM_BACKEND", raising=False)
    assert resolve_ibm_backend_name(None, config_quantum_backend="ibm_torino") == "ibm_torino"


def test_ibm_isa_transpile_and_apply_layout_smoke():
    """Regression: EstimatorV2 requires ISA circuits; transpile + apply_layout on Pauli Z list."""
    from qiskit import QuantumCircuit
    from qiskit.providers.fake_provider import GenericBackendV2
    from qiskit.quantum_info import SparsePauliOp
    from qiskit.transpiler.preset_passmanagers import generate_preset_pass_manager

    n = 4
    qc = QuantumCircuit(n)
    for i in range(n):
        qc.h(i)
    qc.rzz(0.1, 0, 1)

    backend = GenericBackendV2(
        num_qubits=n, basis_gates=["cx", "rz", "sx", "x", "id"]
    )
    pm = generate_preset_pass_manager(optimization_level=1, backend=backend)
    isa = pm.run(qc)
    obs = [
        SparsePauliOp.from_list(
            [("".join("Z" if j == k else "I" for j in range(n)), 1.0)]
        )
        for k in range(n)
    ]
    assert isa.layout is not None
    obs_isa = [o.apply_layout(isa.layout, isa.num_qubits) for o in obs]
    assert len(obs_isa) == n


def test_run_z_expectations_statevector_shape():
    from qminiwasm.quantum.qiskit_qaoa import run_z_expectations_statevector

    n, L = 4, 3
    w = np.random.randn(n)
    g = np.random.randn(L) * 0.1
    b = np.random.randn(L) * 0.1
    bias = np.random.randn(n) * 0.01
    coup = np.random.randn(n, n) * 0.01
    coup = (coup + coup.T) * 0.5
    ev = run_z_expectations_statevector(n, L, w, g, b, bias, coup)
    assert ev.shape == (n,)
    assert np.all(np.isfinite(ev))


def test_neural_qaoa_qiskit_branch_runs():
    """NeuralQAOA.quantum_circuit uses Qiskit statevector path."""
    import torch

    from qminiwasm.quantum.qaoa_integration import NeuralQAOA, QAOAConfig

    cfg = QAOAConfig(
        num_layers=2,
        execution_mode="qiskit_statevector",
        use_neural_prediction=False,
    )
    nq = 4
    m = NeuralQAOA(nq, cfg)
    w = torch.randn(nq)
    g = m.gamma
    b = m.beta
    out = m.quantum_circuit(w, g, b)
    assert out.shape == (nq,)
    assert torch.isfinite(out).all()
