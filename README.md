# qminiwasm-core

`qminiwasm-core` bridges WebAssembly execution, edge-oriented ternary ML, and optional quantum routing in one runtime stack.

## Choose Your Path

### Door A: Strategic and Executive

Start here if you need the why, architecture, and strategic value without operational commands.

- Strategic overview: [docs/Project-Goals.md](docs/Project-Goals.md)
- Architecture narrative: [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md)
- Research and theory: [wiki/README.md](wiki/README.md)

### Door B: Tactical and Operator

Start here if you need to run the system, deploy infrastructure, or operate training workloads.

- Happy-path onboarding: [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)
- Operations and infrastructure: [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)
- Hardware and advanced runtime config: [docs/INSTALL_TORCH_XPU.md](docs/INSTALL_TORCH_XPU.md), [docs/SYCL-Integration.md](docs/SYCL-Integration.md)

## Door A: Strategic View

### Problem Framing

Modern edge AI has three recurring constraints:

- constrained compute and memory budgets
- trust and isolation requirements at the edge boundary
- routing and optimization overhead when model choices become dynamic

`qminiwasm-core` addresses those constraints by combining deterministic WebAssembly execution with ternary-weight modeling and optional quantum-assisted routing.

### Core Architecture (Conceptual)

- **WebAssembly sandbox** for deterministic execution and stronger workload isolation
- **Ternary model representation** (`{-1,0,1}`) for smaller state and predictable inference math
- **Python orchestration layer** that coordinates training and routing decisions
- **Quantum routing path (optional)** through Qiskit/IBM Runtime for QAOA-style expectation signals

```text
Edge Input -> Wasm Runtime -> Python Orchestrator -> Quantum Router (optional) -> Routed Output
```

### Value Proposition

- Smaller model footprint and deterministic execution characteristics
- Composable runtime where classical and quantum paths can coexist
- Clear separation between concept-level architecture and operational workflows

### Read Next (Strategic)

- [docs/Project-Goals.md](docs/Project-Goals.md)
- [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md)
- [wiki/Business-Value.md](wiki/Business-Value.md)
- [wiki/Mathematical-Formulation.md](wiki/Mathematical-Formulation.md)

## Door B: Tactical View

### 0 to 1 Quickstart

Use the guided happy path here:

- [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)

What you get:

- a local baseline run
- a local HTTP endpoint on `localhost:8080`
- one known-good validation command

### Operator Workflows

All deep operational procedures are consolidated here:

- [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)

Includes:

- RunPod and OpenTofu workflows
- serverless endpoint and worker patterns
- SSH sync and remote execution patterns
- advanced CLI and environment controls

### Advanced Technical References

- Training data and metrics: [docs/TRAINING_DATA.md](docs/TRAINING_DATA.md)
- Quantum execution modes: [docs/QUANTUM_QISKIT.md](docs/QUANTUM_QISKIT.md)
- Cascade RL and MOPD: [docs/CASCADE_AND_MOPD.md](docs/CASCADE_AND_MOPD.md)
- Environment variables: [docs/environment-variables.md](docs/environment-variables.md)

## Journey of a Vector

The core innovation is documented as a literal data-path walkthrough:

- [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md)

This trace explains how a vector moves from Wasm-edge execution into the Python router, through optional IBM Qiskit execution, and back into the classical result path.

## Documentation Map

- Getting started: [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)
- Operations: [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)
- Training and data: [docs/TRAINING_DATA.md](docs/TRAINING_DATA.md)
- Quantum integration: [docs/QUANTUM_QISKIT.md](docs/QUANTUM_QISKIT.md)
- Hardware acceleration: [docs/INSTALL_TORCH_XPU.md](docs/INSTALL_TORCH_XPU.md), [docs/SYCL-Integration.md](docs/SYCL-Integration.md)
- Wiki index: [wiki/README.md](wiki/README.md)

## Project Structure (Condensed)

```text
qminiwasm-core/
|- qminiwasm/                  # core model, wasm host, quantum router, training/serve (`qminiwasm.engine`)
|- docs/                       # canonical repository documentation
|- wiki/                       # extended strategic/research documentation
|- infra/runpod/               # OpenTofu stack for RunPod workflows
`- training-wui/               # operator UI and automation layer
```

## License

MIT License
