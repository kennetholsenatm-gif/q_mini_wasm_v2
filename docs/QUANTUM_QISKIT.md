# Qiskit and IBM Quantum (training)

The default MoE block (`HybridQuantumMoE`) is a no-op. You can run the **same QAOA ansatz** as the old PennyLane path using **Qiskit** instead:

| Mode | Backend | Notes |
|------|-----------|--------|
| `pennylane` | (default) | Identity router; no quantum execution in the MoE. |
| `qiskit_statevector` | `qiskit.primitives.StatevectorEstimator` | Local, exact ⟨Z_i⟩; requires `pip install qiskit`. |
| `qiskit_ibm` | `qiskit_ibm_runtime.EstimatorV2` | Real IBM device; requires `IBM_QUANTUM_API_TOKEN`, `IBM_BACKEND_NAME`, and `pip install qiskit-ibm-runtime`. |

## Configuration

- **TOML** (`[hardware]`): `qaoa_execution_mode`, optional `ibm_qaoa_shots`, optional `quantum_execution_policy`.
- **Environment**: `QMINIWASM_QAOA_EXECUTION`, `IBM_QAOA_SHOTS`, `NUM_QUBITS`, `QAOA_LAYERS`, `IBM_BACKEND_NAME` (or `QUANTUM_BACKEND` when it is an `ibm_*` device name — training-wui sets this), `IBM_QUANTUM_API_TOKEN`, `QUANTUM_EXECUTION_POLICY`.

When **`QUANTUM_EXECUTION_POLICY=hardware_only`** (e.g. training-wui “Hardware only”) and `QMINIWASM_QAOA_EXECUTION` is still the default **`pennylane`**, the engine **automatically sets** `qaoa_execution_mode` to **`qiskit_ibm`** so the MoE is not left as an identity block. Explicit `QMINIWASM_QAOA_EXECUTION` (or TOML `qaoa_execution_mode`) still wins if you set something other than `pennylane`.

The IBM **device name** for Runtime is resolved from, in order: `IBM_BACKEND_NAME`, **`[hardware].quantum_backend`** in the loaded TOML (passed through `EngineConfig` into the model — use this when the worker does not inherit `QUANTUM_BACKEND` from the shell), then `QUANTUM_BACKEND` in the environment. The name must look like an IBM device (e.g. `ibm_torino`).

Gradients do **not** flow through IBM hardware execution; expectations are used as a **detached** signal mixed into the hidden state via trainable linear layers.

IBM Runtime only accepts native ``rzz`` angles in **[0, π/2]**. The ``qiskit_ibm`` build path maps each two-qubit angle with ``|γ·coupling|`` clipped to that window so validation passes (statevector mode is unchanged).

Circuits must be **ISA-compliant** (transpiled to the backend’s native gates and layout). The IBM path uses ``generate_preset_pass_manager(..., backend=backend)`` and maps each ``SparsePauliOp`` with ``apply_layout`` before ``EstimatorV2.run``.

**Quota / usage limit:** If IBM Runtime rejects the job (e.g. instance has met its usage limit), the code can **fall back** to local ``qiskit_statevector`` (exact ⟨Z_i⟩, no queue). This is **on by default**; set ``QMINIWASM_IBM_FALLBACK_STATEVECTOR=0`` to fail hard instead of falling back.

## Dependencies

`qiskit` and `qiskit-ibm-runtime` are **core dependencies** of this package (`install_requires` / `pyproject.toml` `dependencies`). `requirements/quantum.txt` includes them for Docker/Incus installs (`-r requirements/docker.txt`).

```bash
pip install -e .
# or
pip install -e ".[training]"
```
