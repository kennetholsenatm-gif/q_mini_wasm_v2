# Core API Reference

## `core::ternary` — Trit Operations

### `Trit` (enum)

```cpp
enum class Trit : int8_t {
    NEGATIVE = -1,
    ZERO     =  0,
    POSITIVE =  1
};
```

### GF(3) Operations

| Function | Signature | Description |
|---|---|---|
| `gf3_add` | `Trit(Trit a, Trit b)` | Addition mod 3 |
| `gf3_mul` | `Trit(Trit a, Trit b)` | Multiplication mod 3 |
| `gf3_neg` | `Trit(Trit a)` | Negation mod 3 |

---

## `core::stabilizer` — Tableau

### `create_tableau`

```cpp
std::unique_ptr<StabilizerTableau> create_tableau(size_t n_qutrits);
```

Creates a tableau tracking `n_qutrits` qutrits.

### `StabilizerTableau` Methods

| Method | Signature | Complexity |
|---|---|---|
| `apply_hadamard` | `void(size_t q)` | O(n) |
| `apply_phase` | `void(size_t q)` | O(n) |
| `apply_csum` | `void(size_t a, size_t b)` | O(n) |
| `measure` | `Trit(size_t q)` | O(n²) |

---

## `core::moe` — Expert Router

### `ExpertConfig`

```cpp
struct ExpertConfig {
    size_t total_experts;
    size_t active_experts;
    size_t routing_dim;
};
```

### `create_moe_router`

```cpp
std::unique_ptr<MoERouter> create_moe_router(const ExpertConfig& config);
```

### `MoERouter` Methods

| Method | Signature | Description |
|---|---|---|
| `route_topk` | `std::vector<size_t>(const std::vector<Trit>&)` | Top-K expert selection |

---

## `runtime` — Orchestration

### `RuntimeConfig`

```cpp
struct RuntimeConfig {
    size_t worker_threads;
    size_t max_queue_size;
    bool enable_monitoring;
    bool enable_sympy_debug;
};
```

### `create_orchestrator`

```cpp
std::unique_ptr<RuntimeOrchestrator> create_orchestrator(const RuntimeConfig& config);
```

### `RuntimeOrchestrator` Methods

| Method | Signature | Description |
|---|---|---|
| `submit_tableau_update` | `std::future<void>(...)` | Async tableau op |
| `wait_all` | `void()` | Block until done |
</content>
<write_to_file>
<path>docs/guides/building.md</path>
<content>
# Build Guide

## Prerequisites

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14+
- Git

## Quick Build

```bash
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2/q_mini_wasm_v2
mkdir build && cd build
cmake ..
cmake --build .
```

## Build Options

| Flag | Default | Description |
|---|---|---|
| `BUILD_TESTS` | `ON` | Build test executables |
| `USE_SYCL` | `OFF` | Enable SYCL acceleration |
| `BUILD_WASM` | `OFF` | Build WebAssembly target |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared library |

## Running Tests

```bash
cd build
ctest --output-on-failure
```

## Platform Notes

### Windows (MSVC)

```powershell
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

### Linux (GCC/Clang)

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
```

### macOS (Clang)

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(sysctl -n hw.ncpu)
```
</content>
<write_to_file>
<path>docs/guides/sycl-setup.md</path>
<content>
# SYCL Setup Guide

## Intel oneAPI

1. Download Intel oneAPI Base Toolkit from intel.com
2. Install with default options
3. Source the environment:

```bash
source /opt/intel/oneapi/setvars.sh
```

## Build with SYCL

```bash
cmake -DUSE_SYCL=ON -DCMAKE_CXX_COMPILER=icpx ..
cmake --build . -j$(nproc)
```

## Verify SYCL

```bash
./q_mini_wasm_v2_tests --gtest_filter="*sycl*"
```

## Troubleshooting

| Issue | Solution |
|---|---|
| `IntelSYCL not found` | Source `setvars.sh` first |
| GPU not detected | Check `sycl-ls` output |
| Link errors | Ensure DPC++ runtime installed |
</content>
<write_to_file>
<path>docs/guides/contributing.md</path>
<content>
# Contributing Guide

## Process

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/name`
3. Make changes
4. Ensure CI passes
5. Submit a pull request

## Code Standards

- C++17 standard
- Headers use `#pragma once`
- Namespace: `q_mini_wasm_v2::`
- All public APIs must have doc comments

## Documentation Standards

Follow Cognitive Ergonomics principles:

- Line length ≤ 75 characters
- Paragraphs ≤ 4 lines
- Headers every ±200 words
- Code blocks ≤ 15 lines
- Navigation depth ≤ 3 levels

## Commit Messages

```
type: short description

Longer explanation if needed.
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`

## Testing

All changes must pass:

```bash
ctest --output-on-failure
```
</content>
<write_to_file>
<path>docs/decisions/adr-001-ternary-over-binary.md</path>
<content>
# ADR-001: Ternary Over Binary

## Status

Accepted

## Context

Binary neural networks (1-bit) lose significant expressivity.
Floating-point (32-bit) is energy-prohibitive for edge deployment.

## Decision

Use **ternary state space** `{+1, 0, -1}` as the fundamental
computation unit.

## Rationale

| Metric | Binary | Ternary | FP32 |
|---|---|---|---|
| Values | 2 | 3 | 2^32 |
| Bits/trit | 1 | 1.58 | 32 |
| Energy/op | ~0.1 pJ | <1 pJ | ~3.7 pJ |
| Expressivity | Low | High | Very High |

**Ternary achieves the best energy-expressivity tradeoff**
for edge AI inference.

## Consequences

- All arithmetic must be GF(3) (mod 3)
- Hardware needs ternary ALU or software emulation
- 5-trit packing achieves 99.06% entropy efficiency