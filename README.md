# qminiwasm-core

Hybrid classical-quantum machine learning runtime: deterministic WebAssembly execution, ternary-weight inference, and triggered QAOA routing in one stack.

## Why this stack

Remote edge nodes rarely fail because the math is exotic—they fail because **memory, bandwidth, and thermal budgets** are fixed while models grow, because **routing and capacity decisions** get expensive when experts and paths change under load, and because **trust boundaries** at the edge require isolation you can reason about operationally.

**Ternary weights and TPEM-style packing** answer the footprint problem: representing parameters in `{-1, 0, 1}` with dense packing (Ternary-Packed Memory Enclave, **TPEM**) keeps more capacity in the same bytes and memory traffic than full-precision tensors at the same width. **QAOA-style routing is a triggered state**, used only when classical assignment over experts and paths crosses a hard combinatorial wall.

Patterns for **Edge Cognitive Looping (ECL)**, the continuous edge-to-host feedback mechanism for hard examples, and **Zero-Trust Ephemeral Enrollment (ZTEE)**, the cryptographic handshake and identity bootstrap required for secure node startup, matter once those constraints are clear; start with operations and the vector journey, then read [docs/IDENTITY_STACK_REFERENCE.md](docs/IDENTITY_STACK_REFERENCE.md) when wiring trust and identity lifecycle.

Execution stays in a **sandboxed WebAssembly module** with bounded linear memory; **packed ternary state** flows through native helpers where available; **routing logic in `qminiwasm`** can delegate to IBM Qiskit Runtime or local simulators when configured. **Inference and training orchestration stay in Python** while the edge contract stays explicit between host runtimes (`wasmtime` in development, WasmEdge-oriented paths in-tree for production-shaped deployments).

## Operational Tier Matrix

| State | Name | Runtime boundary | Purpose | Transition rule |
|------|------|------------------|---------|-----------------|
| **State 1 (Always-On)** | **Ternary WASM Inference** | Local CPU + bounded WASM linear memory | Deterministic forward passes and normal expert/path assignment under strict local latency and thermal limits | Default operating mode |
| **State 2 (Triggered)** | **QAOA Routing (Quantum Approximate Optimization Algorithm)** | Python orchestrator + Qiskit backend (statevector or IBM Runtime) | Solve combinatorial routing when classical assignment can no longer satisfy budget constraints | Enter only when State 1 exceeds configured compute/latency wall |

### Enclave tiering (Tier 1-5)

| Tier | Enclave class | Typical EF / linear memory | Runtime default memory boundary | Role (short) |
|------|----------------|----------------------------|----------------------------------|--------------|
| **1** | **Micro-Enclaves** | Sub-250 MB | `4096` pages (~256 MiB), Memory64 off | Ultra-edge sensing, fast cold paths, routing QUBO seeds |
| **2** | **Meso-Enclaves** | ~2 GB | `32768` pages (~2 GiB), Memory64 off | Laptops / gateways; ESI decode; baseline ECL |
| **3** | **Macro-Enclaves** | ~8 GB (Memory64) | `131072` pages (~8 GiB), Memory64 on, `WASM_MEMORY64_MAX_MB=8192` | Deep ECL, QAHR, high-fidelity synthesis on unified-memory workstations |
| **4** | **Workgroup Enclaves** | ~16 GB (host-orchestrated) | `262144` pages (~16 GiB), Memory64 on, `WASM_MEMORY64_MAX_MB=16384` | Fleet-level workgroup orchestration with same ECL/CGE/QAHR semantics |
| **5** | **Enterprise Core Enclaves** | ~256 GB+ (host-orchestrated) | `4194304` pages (~256 GiB), Memory64 on, `WASM_MEMORY64_MAX_MB=262144` | Enterprise core memory envelopes and centralized control-plane policy |

**Preset + override resolution:** explicit overrides (`max_linear_memory_pages`, `wasm_memory64_max_mb`, `use_memory64`) win over tier defaults; tier defaults win over generic runtime defaults.

### When to use Quantum Routing

State 2 engages at an explicit operational wall, not by preference. **Example policy (control-plane configured):** State 2 engages when classical assignment over 64 or more experts exceeds the 50ms latency budget, pausing local execution and delegating combinatorial search to the Qiskit backend. After the route is resolved, execution resumes in State 1.

## Getting started

Clone the repository, create a Python 3.10+ environment, and install in editable mode with development tooling:

```bash
pip install -e ".[dev]"
```

Optional extras are defined in [pyproject.toml](pyproject.toml): `serve` (HTTP inference), `wasm`, `training`, and `security`. Use the guided happy path next—local baseline, localhost endpoint, and a known-good validation command:

- [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)

For production-style operations (RunPod/OpenTofu, serverless workers, SSH sync, environment controls):

- [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)

Strategic context, research notes, and extended wiki index:

- [wiki/README.md](wiki/README.md)
- [docs/Q-Mini-WASM_ Edge AI Taxonomy.md](docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)

Architecture goals and the narrative “journey of a vector”:

- [docs/Project-Goals.md](docs/Project-Goals.md)
- [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md)

Hardware and accelerated runtime configuration (when you outgrow CPU defaults):

- [docs/INSTALL_TORCH_XPU.md](docs/INSTALL_TORCH_XPU.md), [docs/SYCL-Integration.md](docs/SYCL-Integration.md)

## From input to decision

The path below follows the **physical lifecycle of one tensor**, from host input to resumed execution after routing. For the extended walkthrough, see [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md).

### Step 1 - Python host receives and prepares the tensor

The Python orchestrator (`qminiwasm.model.QMiniWASM`) receives hidden-state tensors and executes through `qminiwasm/wasm_host/engine.py` in a bounded Wasm store. The host captures pre/post linear-memory images as raw bytes so state can be encoded, paused, and resumed deterministically.

### Step 2 - Quantization to ternary logic

Model values are quantized into ternary states `{-1, 0, 1}` and mapped to base-3 digits `{0, 1, 2}` for transport. The canonical conversion and validation path is implemented in `qminiwasm/wasm_host/trit_pack.py`.

### Step 3 - Bitwise packing and linear-memory transfer

Ternary values are packed at **5 trits per byte** (MSB-first base-3 packing) before movement across runtime boundaries. Host paths read/write these bytes through Wasm linear memory exports, including direct read/write helpers (`host_tensor.py`) and runtime write paths (`trit_wasm_runtime.py`).

### Step 4 - State 1 execution in WASM linear memory

In State 1, deterministic execution stays local: the Wasm module runs with bounded memory, and the host reads linear memory snapshots for feature encoding (`memory_encode.py`) and execution telemetry (`engine.py`).

### Step 5 - State 2 handoff and mechanical resume

If the trigger wall is crossed, the orchestrator builds an escalation payload (`qminiwasm/cognitive/escalation.py`) with linear memory and control metadata. The routing backend returns an optimal assignment path; then the Python orchestrator writes resumed state back into the WASM linear-memory buffer (see `write_linear_memory(...)` in `qminiwasm/wasm_host/wles_wasmtime_harness.py`) and execution continues in State 1 using the selected route.

See also top-level `docs/`, `wiki/`, and `infra/` for deeper operator and infrastructure detail.

## Documentation map

- Getting started: [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)
- Operations: [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)
- Identity and trust lifecycle: [docs/IDENTITY_STACK_REFERENCE.md](docs/IDENTITY_STACK_REFERENCE.md)
- Training and data: [docs/TRAINING_DATA.md](docs/TRAINING_DATA.md)
- Quantum: [docs/QUANTUM_QISKIT.md](docs/QUANTUM_QISKIT.md)
- Cascade RL / MOPD: [docs/CASCADE_AND_MOPD.md](docs/CASCADE_AND_MOPD.md)
- Environment variables: [docs/environment-variables.md](docs/environment-variables.md)
- Wiki: [wiki/README.md](wiki/README.md)
- Hardware acceleration: [docs/INSTALL_TORCH_XPU.md](docs/INSTALL_TORCH_XPU.md), [docs/SYCL-Integration.md](docs/SYCL-Integration.md)
- Project taxonomy glossary: [docs/Q-Mini-WASM_ Edge AI Taxonomy.md](docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)

## License

MIT License
