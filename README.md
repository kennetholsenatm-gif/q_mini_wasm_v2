# q_mini_wasm_v2

> **1.58-bit ternary AI framework. Quantum-inspired. Edge-deployable.**

Operates entirely in a ternary state space `{+1, 0, -1}` — no floating-point arithmetic.
Achieves 99.06% entropy efficiency via 5-trit-to-8-bit packing.

[![CI](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/actions/workflows/ci.yml/badge.svg)](https://github.com/kennetholsenatm-gif/q_mini_wasm_v2/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-research-blue)](#license)

---

## What This Is

A C++17 framework for extreme-edge AI inference using:

| Concept | Mechanism |
|---|---|
| **Ternary Compute** | GF(3) arithmetic over `{+1, 0, -1}` — 3× energy reduction vs FP32 |
| **Quantum-Inspired** | Qutrit stabilizer tableau (O(n²) Clifford gates via Gottesman-Knill) |
| **Mixture of Experts** | Tropical geometry routing — sparsity = expressivity |
| **Self-Supervised Learning** | Forward-Forward algorithm — no backpropagation |
| **Edge Runtime** | SYCL multi-core + Flash-CIM integration |

```
Input → Absmean Quantizer → Clifford Shadow → MoE Router → Forward-Forward Experts → Output
         (continuous→trit)    (hash encoding)   (Top-K select)   (local learning)
```

---

## Get Started in 60 Seconds

### 1. Clone & Build

```bash
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2/q_mini_wasm_v2
mkdir build && cd build
cmake .. && cmake --build .
```

### 2. Run Tests

```bash
ctest --output-on-failure
```

### 3. Use in Your Code

```cpp
#include "core/ternary/trit.hpp"
#include "core/stabilizer/tableau.hpp"

using namespace q_mini_wasm_v2;

auto tableau = core::stabilizer::create_tableau(4);
tableau->apply_hadamard(0);
tableau->apply_csum(0, 1);
```

**That's it.** You now have a running ternary neural network core.

---

## How It Works

<details>
<summary><strong>Ternary State Space</strong> — the foundation</summary>

All computation happens in GF(3) over three symbols:

| Trit | Value | Binary Equivalent |
|---|---|---|
| `+1` | Positive | ~1 |
| `0` | Zero | ~0 |
| `-1` | Negative | ~-1 |

**Why ternary?** 3 values × 5 trits = 243 states, packing into 8 bits (256 values) achieves **99.06% entropy efficiency** — near the Shannon limit.

Energy per operation: **<1 pJ** vs **~3.7 pJ** for FP32.

</details>

<details>
<summary><strong>Stabilizer Tableau</strong> — quantum-inspired parallelism</summary>

Tracks n qutrits using a 2n×2n matrix over GF(3). Clifford gates (Hadamard, Phase, Controlled-SUM) update in **O(n²)** time — no exponential overhead.

The Gottesman-Knill theorem guarantees efficient classical simulation of these operations.

</details>

<details>
<summary><strong>Mixture of Experts</strong> — tropical geometry routing</summary>

Uses max-plus semiring algebra:
- **Tropical addition** = `max(a, b)`
- **Tropical multiplication** = `a + b`

Top-K routing selects K active experts from N total. Hypersimplex capacity = C(N,K) regions.

Sparsity *is* expressivity.

</details>

<details>
<summary><strong>Forward-Forward Learning</strong> — no backpropagation</summary>

Each layer learns independently using a "goodness" metric:
- **Positive samples**: maximize goodness (tropical inner product)
- **Negative samples**: minimize goodness

Weight updates are Hebbian — local, gradient-free, parallelizable.

</details>

<details>
<summary><strong>SYCL Acceleration</strong> — hardware-agnostic parallelism</summary>

Parallel tableau updates across GPU/CPU via SYCL:
- Vectorized modulo-3 operations
- Sub-group parallelism for trit arithmetic
- Thread pool orchestration for async task management

Enable with: `cmake -DUSE_SYCL=ON ..`

</details>

---

## Architecture

```
q_mini_wasm_v2/
├── core/
│   ├── ternary/        # Trit types, GF(3) arithmetic
│   ├── stabilizer/     # Qutrit tableau, Clifford synthesis
│   ├── moe/            # Tropical geometry router
│   ├── learning/       # Forward-Forward implementation
│   ├── ingestion/      # Absmean quantizer, Clifford shadows
│   ├── steane/         # Fault-tolerant Steane code
│   ├── network.hpp     # Unified network orchestrator
│   └── network.cpp
├── runtime/            # Async thread pool, task scheduling
├── sycl/               # SYCL kernel declarations
├── tests/              # Unit + integration tests
├── wui/                # Cognitive-ergonomic web UI
├── dll/                # Shared library API
└── go/                 # Go bindings
```

**→ [Full Architecture Deep-Dive](docs/architecture/overview.md)**

---

## Build Options

| Flag | Default | Effect |
|---|---|---|
| `BUILD_TESTS` | `ON` | Build test suite |
| `USE_SYCL` | `OFF` | Enable SYCL GPU acceleration |
| `BUILD_WASM` | `OFF` | Build WebAssembly target (requires Emscripten) |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared library |

```bash
# Full build with SYCL
cmake -DUSE_SYCL=ON -DBUILD_TESTS=ON ..
cmake --build .

# WASM build
cmake -DBUILD_WASM=ON -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake ..
cmake --build .
```

---

## Performance

| Operation | Complexity | Energy (est.) |
|---|---|---|
| Stabilizer Tableau Update | O(n²) | <1 pJ/op |
| MoE Routing (Top-K) | O(n·k) | <0.5 pJ/op |
| Forward-Forward Layer | O(n·m) | <0.3 pJ/op |
| Trit Pack/Unpack | O(1) | <0.1 pJ/op |

*FP32 baseline: ~3.7 pJ/op*

---

## Documentation

| Resource | Audience |
|---|---|
| **This README** | Everyone — quick start + overview |
| **[Architecture Docs](docs/architecture/)** | Engineers — subsystem deep-dives |
| **[API Reference](docs/api/)** | Integrators — function signatures |
| **[Build Guides](docs/guides/)** | DevOps — platform-specific setup |
| **[Research Papers](docs/research/)** | Researchers — theoretical foundations |
| **[GitHub Wiki](../../wiki)** | Deep technical reference |

---

## Contributing

See **[Contributing Guide](docs/guides/contributing.md)**.

TL;DR:
1. Fork → branch → commit
2. All PRs require passing CI
3. Follow the Cognitive Ergonomics documentation standards

---

## License

This project is part of the q_mini_wasm research framework.