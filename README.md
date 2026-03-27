# qminiwasm-core

**[Glossary](docs/GLOSSARY.md)** — terms and acronyms used below.

## Run locally

Python 3.10+ and `pip`. From the repository root:

```bash
git clone <repository-url>
cd qminiwasm-core
pip install -e .
pip install -e ".[serve]"
uvicorn qminiwasm.engine.serve:app --host 127.0.0.1 --port 8080
```

In a second terminal:

```bash
curl http://127.0.0.1:8080/health
```

```bash
python -c "import json,urllib.request;data=json.dumps({'hidden_states': [[0.0]*4096]}).encode();req=urllib.request.Request('http://127.0.0.1:8080/infer', data=data, headers={'Content-Type':'application/json'});print(urllib.request.urlopen(req).read().decode())"
```

Step-by-step narrative, prerequisites, and expected JSON: **[docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)**.

For development tooling (lint, tests): `pip install -e ".[dev]"`. Optional extras: [pyproject.toml](pyproject.toml) — `wasm`, `training`, `security`.

**Remote GPU / OpenTofu / RunPod:** use **[docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)** only; do not treat cloud provisioning as part of the local quickstart.

---

Large machine-learning models are difficult to run on edge devices because **memory, thermal, and bandwidth budgets** are fixed while model capacity grows. **qminiwasm-core** is a **hybrid classical–quantum ML runtime** that keeps a **sandboxed WebAssembly** execution boundary on the host so inference and training orchestration stay predictable next to Python. Optional cloud backends (for example Qiskit) apply only when operational policy triggers them.

## Ecosystem context

| Piece | Role |
|--------|------|
| **qminiwasm-core** (this repo) | **ML inference/training runtime** with WASM sandboxing and optional quantum-assisted routing. |
| **OmniGraph** | Separate **infrastructure** graph workspace (OpenTofu/Terraform, Ansible, CI context)—not an ML engine. |

**There is no shipped integration** between OmniGraph and this repository: no shared schema, no API bridge, and **OmniGraph does not visualize WASM enclaves or model graphs** unless you model that infrastructure yourself in OmniGraph.

**Optional operator workflow:** you may use OmniGraph to reason about **IaC** that provisions training hosts (for example paths under `infra/runpod/`). That is **manual** alignment of tools, not a built-in connector.

## How we stay within the constraint

Remote edge nodes rarely fail because the math is exotic—they fail because **memory, bandwidth, and thermal budgets** are fixed while models grow, because **routing and capacity decisions** get expensive when experts and paths change under load, and because **trust boundaries** at the edge require isolation you can reason about operationally.

Techniques for **smaller footprints**, **triggered quantum routing**, and **identity at enrollment** are summarized in the **[Glossary](docs/GLOSSARY.md)** and in depth in [docs/Q-Mini-WASM_ Edge AI Taxonomy.md](docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md). When wiring trust and identity lifecycle, start with [docs/IDENTITY_STACK_REFERENCE.md](docs/IDENTITY_STACK_REFERENCE.md).

Execution stays in a **sandboxed WebAssembly module** with bounded linear memory; host-native helpers apply where available. **Routing** in `qminiwasm` can delegate to IBM Qiskit Runtime or local simulators when configured. **Inference and training orchestration stay in Python** while the edge contract stays explicit between host runtimes (`wasmtime` in development, WasmEdge-oriented paths in-tree for production-shaped deployments).

## Documentation map

- Glossary (acronyms): [docs/GLOSSARY.md](docs/GLOSSARY.md)
- Getting started: [docs/getting-started/QUICKSTART_0_TO_1.md](docs/getting-started/QUICKSTART_0_TO_1.md)
- Operations: [docs/operations/OPERATIONS_RUNBOOK.md](docs/operations/OPERATIONS_RUNBOOK.md)
- Identity and trust lifecycle: [docs/IDENTITY_STACK_REFERENCE.md](docs/IDENTITY_STACK_REFERENCE.md)
- Training and data: [docs/TRAINING_DATA.md](docs/TRAINING_DATA.md)
- Quantum: [docs/QUANTUM_QISKIT.md](docs/QUANTUM_QISKIT.md)
- Cascade RL / MOPD: [docs/CASCADE_AND_MOPD.md](docs/CASCADE_AND_MOPD.md)
- Environment variables (secrets / APIs only): [docs/environment-variables.md](docs/environment-variables.md); CI and container overrides: [docs/ENV_CI_OVERRIDES.md](docs/ENV_CI_OVERRIDES.md)
- Wiki: [wiki/README.md](wiki/README.md)
- Hardware acceleration: [docs/INSTALL_TORCH_XPU.md](docs/INSTALL_TORCH_XPU.md), [docs/SYCL-Integration.md](docs/SYCL-Integration.md)
- Extended taxonomy (research vocabulary): [docs/Q-Mini-WASM_ Edge AI Taxonomy.md](docs/Q-Mini-WASM_%20Edge%20AI%20Taxonomy.md)
- Architecture goals: [docs/Project-Goals.md](docs/Project-Goals.md)
- Journey of a vector: [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md)

## Architecture (read next)

### Operational tier matrix

| State | Name | Runtime boundary | Purpose | Transition rule |
|------|------|------------------|---------|-----------------|
| **State 1 (Always-On)** | **Ternary WASM Inference** | Local CPU + bounded WASM linear memory | Deterministic forward passes and normal expert/path assignment under strict local latency and thermal limits | Default operating mode |
| **State 2 (Triggered)** | **QAOA Routing (Quantum Approximate Optimization Algorithm)** | Python orchestrator + Qiskit backend (statevector or IBM Runtime) | Solve combinatorial routing when classical assignment can no longer satisfy budget constraints | Enter only when State 1 exceeds configured compute/latency wall |

#### Enclave tiering (Tier 1–5)

| Tier | Enclave class | Typical EF / linear memory | Runtime default memory boundary | Role (short) |
|------|----------------|----------------------------|----------------------------------|--------------|
| **1** | **Micro-Enclaves** | Sub-250 MB | `4096` pages (~256 MiB), Memory64 off | Ultra-edge sensing, fast cold paths, routing QUBO seeds |
| **2** | **Meso-Enclaves** | ~2 GB | `32768` pages (~2 GiB), Memory64 off | Laptops / gateways; ESI decode; baseline ECL |
| **3** | **Macro-Enclaves** | ~8 GB (Memory64) | `131072` pages (~8 GiB), Memory64 on, `WASM_MEMORY64_MAX_MB=8192` | Deep ECL, QAHR, high-fidelity synthesis on unified-memory workstations |
| **4** | **Workgroup Enclaves** | ~16 GB (host-orchestrated) | `262144` pages (~16 GiB), Memory64 on, `WASM_MEMORY64_MAX_MB=16384` | Fleet-level workgroup orchestration with same ECL/CGE/QAHR semantics |
| **5** | **Enterprise Core Enclaves** | ~256 GB+ (host-orchestrated) | `4194304` pages (~256 GiB), Memory64 on, `WASM_MEMORY64_MAX_MB=262144` | Enterprise core memory envelopes and centralized control-plane policy |

**Preset + override resolution:** explicit overrides (`max_linear_memory_pages`, `wasm_memory64_max_mb`, `use_memory64`) win over tier defaults; tier defaults win over generic runtime defaults.

#### When to use quantum routing

State 2 engages at an explicit operational wall, not by preference. **Example policy (control-plane configured):** State 2 engages when classical assignment over 64 or more experts exceeds the 50ms latency budget, pausing local execution and delegating combinatorial search to the Qiskit backend. After the route is resolved, execution resumes in State 1.

### From input to decision

The path below follows the **physical lifecycle of one tensor**, from host input to resumed execution after routing. For the extended walkthrough, see [docs/architecture/JOURNEY_OF_A_VECTOR.md](docs/architecture/JOURNEY_OF_A_VECTOR.md).

#### Step 1 — Python host receives and prepares the tensor

The Python orchestrator (`qminiwasm.model.QMiniWASM`) receives hidden-state tensors and executes through `qminiwasm/wasm_host/engine.py` in a bounded Wasm store. The host captures pre/post linear-memory images as raw bytes so state can be encoded, paused, and resumed deterministically.

#### Step 2 — Quantization to ternary logic

Model values are quantized into ternary states `{-1, 0, 1}` and mapped to base-3 digits `{0, 1, 2}` for transport. The canonical conversion and validation path is implemented in `qminiwasm/wasm_host/trit_pack.py`.

#### Step 3 — Bitwise packing and linear-memory transfer

Ternary values are packed at **5 trits per byte** (MSB-first base-3 packing) before movement across runtime boundaries. Host paths read/write these bytes through Wasm linear memory exports, including direct read/write helpers (`host_tensor.py`) and runtime write paths (`trit_wasm_runtime.py`).

#### Step 4 — State 1 execution in WASM linear memory

In State 1, deterministic execution stays local: the Wasm module runs with bounded memory, and the host reads linear memory snapshots for feature encoding (`memory_encode.py`) and execution telemetry (`engine.py`).

#### Step 5 — State 2 handoff and mechanical resume

If the trigger wall is crossed, the orchestrator builds an escalation payload (`qminiwasm/cognitive/escalation.py`) with linear memory and control metadata. The routing backend returns an optimal assignment path; then the Python orchestrator writes resumed state back into the WASM linear-memory buffer (see `write_linear_memory(...)` in `qminiwasm/wasm_host/wles_wasmtime_harness.py`) and execution continues in State 1 using the selected route.

See also top-level `docs/`, `wiki/`, and `infra/` for deeper operator and infrastructure detail.

## License

MIT License
