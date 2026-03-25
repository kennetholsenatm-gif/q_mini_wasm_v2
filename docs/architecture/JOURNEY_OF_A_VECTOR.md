# Journey of a Vector

This document traces one vector through `qminiwasm-core` from edge-side Wasm execution to optional IBM Quantum routing and back into a classical output tensor.

## Why this matters

The architecture can look like "magic" because WebAssembly, PyTorch, and Qiskit are all involved. This walkthrough makes each boundary explicit.

## End-to-end flow

```mermaid
flowchart LR
    wasmInput[WasmEdgeInput] --> wasmRuntime[WasmRuntimeExecution]
    wasmRuntime --> vectorTensor[HiddenStateVectorTensor]
    vectorTensor --> pythonModel[QMiniWASM.hybrid_inference]
    pythonModel --> quantumRouter[HybridQuantumMoE]
    quantumRouter --> modeDecision{ExecutionMode}
    modeDecision -->|pennylane| identityPath[IdentityRouting]
    modeDecision -->|qiskit_statevector| localQiskit[LocalStatevectorEstimator]
    modeDecision -->|qiskit_ibm| ibmRuntime[IBMEstimatorV2Runtime]
    ibmRuntime --> fallbackCheck{QuotaOrCapacityError}
    fallbackCheck -->|yes| localQiskit
    fallbackCheck -->|no| zExpectations[ZiExpectations]
    localQiskit --> zExpectations
    identityPath --> mergedOutput[ClassicalResidualMerge]
    zExpectations --> mergedOutput
    mergedOutput --> outputTensor[FinalOutputTensor]
```

## Step-by-step trace

### Step 1: Vector originates in the Wasm edge environment

- The Wasm execution layer runs deterministic module logic and exposes execution state.
- In practical pipelines, edge-side state and features are transformed into fixed-width hidden vectors used by the model path.
- The `QMiniWASM` object owns the Wasm executor and data pipeline orchestration.

## Step 2: Handoff into Python orchestration

- `QMiniWASM` is initialized in `qminiwasm/model.py` and wires together:
  - `WasmExecutor`
  - `TernaryWASMExpert`
  - `HybridQuantumMoE`
- During inference, `QMiniWASM.hybrid_inference(...)` receives `hidden_states` as a tensor and calls the quantum router block.

## Step 3: Router mode decision

The quantum router mode is determined by `qaoa_execution_mode`:

- `pennylane`: identity/no-op routing path
- `qiskit_statevector`: local exact expectation evaluation
- `qiskit_ibm`: IBM Quantum Runtime hardware path

This selection is configured from training/serve config and environment surfaces documented in [../QUANTUM_QISKIT.md](../QUANTUM_QISKIT.md).

## Step 4: Qiskit execution contract

When `qiskit_ibm` is selected:

1. Build the QAOA circuit.
2. Resolve credentials (`IBM_QUANTUM_API_TOKEN` or `QISKIT_IBM_TOKEN`).
3. Resolve backend device name (`IBM_BACKEND_NAME` or equivalent config fallback).
4. Transpile to ISA-compliant circuit for the selected backend.
5. Execute with `EstimatorV2` and collect per-qubit `Z` expectations.

If IBM runtime fails due to quota or capacity conditions, the path can fall back to local `qiskit_statevector` when fallback is enabled.

## Step 5: Return to classical pipeline

- Quantum expectations are converted to a tensor and detached from hardware execution.
- The router projects quantum outputs back into the model dimension and mixes them as a scaled residual.
- The final routed tensor is returned as the model output for downstream serving or training logic.

## Step 6: What is trainable vs detached

- Classical projection and merge layers remain trainable in PyTorch.
- IBM hardware execution itself is a detached signal source (no gradient through the quantum device).
- This creates a hybrid path where quantum results influence routing while preserving stable classical optimization.

## Practical checkpoints

- For conceptual architecture, return to [../../README.md](../../README.md#door-a-strategic-view).
- For operator setup, use [../operations/OPERATIONS_RUNBOOK.md](../operations/OPERATIONS_RUNBOOK.md).
- For exact quantum configuration and constraints, use [../QUANTUM_QISKIT.md](../QUANTUM_QISKIT.md).
