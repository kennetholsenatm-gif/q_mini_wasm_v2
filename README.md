# qminiwasm-core

Hybrid classical–quantum machine learning runtime: deterministic WebAssembly execution, ternary-weight inference, and optional quantum-assisted routing in one stack.

## Why this stack

Remote edge nodes rarely fail because the math is exotic—they fail because **memory, bandwidth, and thermal budgets** are fixed while models grow, because **routing and capacity decisions** get expensive when experts and paths change under load, and because **trust boundaries** at the edge require isolation you can reason about operationally.

**Ternary weights and TPEM-style packing** answer the footprint problem: representing parameters in `{-1, 0, 1}` with dense packing (Ternary-Packed Memory Enclave, **TPEM**) keeps more capacity in the same bytes and memory traffic than full-precision tensors at the same width. **Optional QAOA-style quantum routing** is for when classical assignment over experts and paths becomes the combinatorial bottleneck—not for every forward pass, but for the tier where routing is itself a hard search problem.

Patterns for **edge cognitive looping (ECL)** and **zero-trust enclave enrollment (ZTEE)** matter once those constraints are clear; start with operations and the vector journey, then read [docs/ENCLAVE_LIFECYCLE.md](docs/ENCLAVE_LIFECYCLE.md) when wiring trust and lifecycle.

Execution stays in a **sandboxed WebAssembly module** with bounded linear memory; **packed ternary state** flows through native helpers where available; **routing logic in `qminiwasm`** can delegate to IBM Qiskit Runtime or local simulators when configured. **Inference and training orchestration stay in Python** while the edge contract stays explicit between host runtimes (`wasmtime` in development, WasmEdge-oriented paths in-tree for production-shaped deployments).

## Getting started

Clone the repository, create a Python 3.8+ environment, and install in editable mode with development tooling:

```bash
pip install -e ".[dev]"
```

Optional extras are defined in [pyproject.toml](pyproject.toml): `serve` (HTTP inference), `wasm`, `training`, and `security`. Use the guided happy path next—local baseline, localhost endpoint, and a known-good validation command:

- [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)

For production-style operations (RunPod/OpenTofu, serverless workers, SSH sync, environment controls):

- [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)

Strategic context, research notes, and extended wiki index:

- [wiki/README.md](wiki/README.md)

Architecture goals and the narrative “journey of a vector”:

- [docs/Project-Goals.md](docs/Project-Goals.md)
- [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md)

Hardware and accelerated runtime configuration (when you outgrow CPU defaults):

- [docs/INSTALL_TORCH_XPU.md](docs/INSTALL_TORCH_XPU.md), [docs/SYCL-Integration.md](docs/SYCL-Integration.md)

## From input to decision: where the code lives

The following follows **one logical path** through the repository (not an alphabetical tree). For the full tensor-level trace, see [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md).

### Step 1 — Input enters the execution sandbox

Edge and server hosts load a **WebAssembly module** with deterministic execution and bounded linear memory. In this tree, the WasmEdge-oriented integration and host-side glue live under [`wasmedge-plugin/`](wasmedge-plugin/README.md) (Rust plugin stub for WASI-NN–style delegation) and [`cpp/wasmedge/`](cpp/wasmedge/) / [`cpp/wasm/`](cpp/wasm/) (orchestration, host memory, and WLES-oriented hooks). For the path most developers run first, the Python **wasmtime** host and trit packing are in [`qminiwasm/wasm_host/`](qminiwasm/wasm_host/)—same contract, different host.

### Step 2 — Ternary math at the edge

To shrink working set and memory traffic, weights and activations are handled in **packed ternary** form (three-valued logic with dense packing). The SYCL-oriented stub and packing notes live in [`ternary_packed/`](ternary_packed/README.md); high-throughput CPU kernels and dispatch are under [`cpp/kernels/`](cpp/kernels/) and related [`cpp/pack/`](cpp/pack/) helpers, aligned with the on-wire layout described alongside `qminiwasm.wasm_host.trit_pack`.

### Step 3 — Orchestration and quantum routing evaluation

Once hidden states exist as tensors, [`qminiwasm/`](qminiwasm/) owns the hybrid model, training and serve entrypoints (`qminiwasm.engine`), and routing fabric (including optional Qiskit/IBM Runtime paths documented in [docs/QUANTUM_QISKIT.md](docs/QUANTUM_QISKIT.md)). For **serverless-style deployment** surfaces (handler, container), use [`serverless/`](serverless/) alongside the operations runbook.

See also top-level `docs/`, `wiki/`, and `infra/` for documentation and infrastructure automation.

## Documentation map

- Getting started: [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)
- Operations: [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)
- Enclave lifecycle: [docs/ENCLAVE_LIFECYCLE.md](docs/ENCLAVE_LIFECYCLE.md)
- Training and data: [docs/TRAINING_DATA.md](docs/TRAINING_DATA.md)
- Quantum: [docs/QUANTUM_QISKIT.md](docs/QUANTUM_QISKIT.md)
- Cascade RL / MOPD: [docs/CASCADE_AND_MOPD.md](docs/CASCADE_AND_MOPD.md)
- Environment variables: [docs/environment-variables.md](docs/environment-variables.md)
- Wiki: [wiki/README.md](wiki/README.md)

## License

MIT License
