# Forward-Forward Learning

## No Backpropagation

Each layer learns **independently** using local goodness metrics.
No gradient chain required.

## Goodness Metric

**Tropical inner product**: `goodness = max(x_i × w_i)`

| Sample Type | Goal |
|---|---|
| Positive | Maximize goodness |
| Negative | Minimize goodness |

## Weight Updates

**Hebbian rule** (gradient-free):

```
w_new = w_old + lr × (goodness_pos - goodness_neg) × input
```

## Parallelism

Layers train independently — fully parallelizable
across SYCL work groups.

## Advantages

- No vanishing gradients
- Local learning (no global coordination)
- Hardware-friendly (integer operations)
- Natural fit for ternary state space
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
| *(native SYCL)* | *(always on)* | Configure with **icpx/icx**; see **CONTRIBUTING.md** |
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
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -G Ninja -DCMAKE_CXX_COMPILER=icpx ..
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