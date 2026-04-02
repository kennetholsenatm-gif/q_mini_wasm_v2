# Architecture Overview

## System Design

q_mini_wasm_v2 is a modular C++17 framework organized around five core subsystems.

```
┌─────────────────────────────────────────────────────────────┐
│                    TernaryNeuralNetwork                      │
│                    (network.hpp/cpp)                         │
├─────────┬──────────┬──────────┬──────────┬─────────────────┤
│ Ingest  │ Shadow   │ MoE      │ Learning │ Fault Tolerance │
│ Layer   │ Layer    │ Router   │ Layer    │ Layer           │
├─────────┴──────────┴──────────┴──────────┴─────────────────┤
│                    Runtime Orchestrator                      │
│              (async task scheduling, SYCL)                   │
└─────────────────────────────────────────────────────────────┘
```

## Subsystems

| Subsystem | Location | Purpose |
|---|---|---|
| **Ternary** | `core/ternary/` | Trit types, GF(3) arithmetic |
| **Stabilizer** | `core/stabilizer/` | Qutrit tableau, Clifford gates |
| **MoE** | `core/moe/` | Tropical geometry expert routing |
| **Learning** | `core/learning/` | Forward-Forward algorithm |
| **Ingestion** | `core/ingestion/` | Data quantization, Clifford shadows |
| **Steane** | `core/steane/` | Fault-tolerant error correction |
| **Runtime** | `runtime/` | Thread pool, async orchestration |
| **SYCL** | `sycl/` | GPU/parallel kernel declarations |

## Data Flow

1. **Input** → `AbsmeanQuantizer` converts continuous values to ternary trits
2. **Encoding** → `CliffordShadow` creates hash-based representations
3. **Routing** → `MoERouter` selects Top-K experts via tropical geometry
4. **Inference** → Selected `ForwardForwardLearner` experts process input
5. **Correction** → `QutritSteaneCode` provides fault tolerance
6. **Output** → Final ternary activations returned

## Memory Model

All operations use stack-allocated trit vectors with deterministic sizing.
No heap allocation in the hot path. SYCL kernels use device memory.

## Build Targets

| Target | Description |
|---|---|
| `q_mini_wasm_v2_core` | Static/shared library |
| `q_mini_wasm_v2_tests` | Unit test suite |
| `q_mini_wasm_v2_network_test` | Integration tests |
| `q_mini_wasm_v2_wasm` | WebAssembly (Emscripten) |