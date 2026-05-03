# Build Guide


## Architecture Diagram

```mermaid
graph LR
    subgraph Source
        S1[C++ Sources]
        S2[Headers]
        S3[CMakeLists]
    end
    
    subgraph Build
        B1[Configure]
        B2[Compile]
        B3[Link]
    end
    
    subgraph Test
        T1[Unit Tests]
        T2[Integration]
        T3[Performance]
    end
    
    subgraph Deploy
        D1[Library]
        D2[WASM]
        D3[Plugins]
    end
    
    subgraph CI/CD
        C1[Lint]
        C2[Build Matrix]
        C3[Test Matrix]
        C4[Deploy]
    end
    
    S1 --> B1
    S2 --> B1
    S3 --> B1
    
    B1 --> B2
    B2 --> B3
    
    B3 --> T1
    B3 --> T2
    B3 --> T3
    
    T1 --> D1
    T2 --> D2
    T3 --> D3
    
    C1 --> C2
    C2 --> C3
    C3 --> C4
    
    style S1 fill:#e3f2fd
    style B1 fill:#e8f5e9
    style T1 fill:#fff3e0
    style D1 fill:#fce4ec
    style C1 fill:#f3e5f5
```

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
| *(native SYCL)* | *(always on)* | Main `q_mini_wasm_v2` tree: configure with **icpx/icx**; see CONTRIBUTING.md |
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