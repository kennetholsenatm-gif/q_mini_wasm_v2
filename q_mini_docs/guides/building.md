# Build Guide

## Prerequisites

- **CMake 3.14+**
- **Native `q_mini_wasm_v2`**: a SYCL-capable toolchain — **Intel oneAPI** (`icx` / `icpx`) with **Ninja** or **Visual Studio + `-T IntelLLVM`**, **AdaptiveCpp**, or on Linux **LLVM `clang++` with `-fsycl`**. Plain **MSVC (`cl`) alone cannot configure** this project.
- **Go** (for `qminiwasm`): see repository root **[CONTRIBUTING.md](../../CONTRIBUTING.md)**.

## Recommended layout

Use a dedicated out-of-source directory (for example **`q_mini_wasm_v2/build_sycl`**) so it is never confused with obsolete MSVC-only caches.

## Linux (Intel oneAPI + Ninja)

```bash
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2
source /opt/intel/oneapi/setvars.sh
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=icpx \
  -DCMAKE_C_COMPILER=icx \
  -DBUILD_TESTS=ON
cmake --build q_mini_wasm_v2/build_sycl -j"$(nproc)"
```

## Windows

Use **`powershell -NoProfile -File .\scripts\build_qminiwasm.ps1`** (Ninja + `icx-cl`, Release) or follow the manual **`icx`** / **`-T IntelLLVM`** steps in **[CONTRIBUTING.md](../../CONTRIBUTING.md)**. Run CMake from an **Intel oneAPI** environment (`setvars.bat`) so the linker resolves Intel libraries.

## CMake options (still toggles)

| Flag | Default | Description |
|---|---|---|
| `BUILD_TESTS` | `ON` | Build test executables |
| `BUILD_WASM` | `OFF` | Build WebAssembly target |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared library |

**SYCL** is always enabled for native `q_mini_wasm_v2` targets (there is no `USE_SYCL` switch).

## Running tests

```bash
cd q_mini_wasm_v2/build_sycl && ctest --output-on-failure
```

## More reading

- **[q_mini_docs/guides/sycl-setup.md](sycl-setup.md)** — Intel environment and verification
- **[q_mini_docs/TRAINING_THROUGHPUT.md](../TRAINING_THROUGHPUT.md)** — training DLL and throughput
