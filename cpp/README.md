# Native C++ (`cpp/`)

CMake project (C++23) implementing packed ternary weights, CPUID-dispatched matvec (scalar / AVX2 / optional AVX-512 TU), a JSON-subset grammar mask toy, OTA state machine (`std::expected`), residual KV XOR blocks, QUBO + NLopt COBYLA (optional), and optional gRPC / SYCL / WasmEdge targets.

## Default configure (tests + core)

```bash
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/qminiwasm_cpp_tests
```

### Native quantum (optional)

With LibTorch on `CMAKE_PREFIX_PATH` / `LIBTORCH_ROOT`:

```bash
cmake -S cpp -B build -DQMINIWASM_WITH_QUANTUM=ON
cmake --build build
./build/qminiwasm_cpp_tests
```

C API: `qmw_openqasm_expval_pauli_z0` and static capability string `qmw_native_quantum_stack_summary` ([`quantum/quantum_bridge_c_api.h`](quantum/quantum_bridge_c_api.h)) for operators and host runtimes. Routing integration stub: `qmw_routing_trinary_expval_pauli_z0` ([`qubo/dqaoa_routing_runtime_c_api.h`](qubo/dqaoa_routing_runtime_c_api.h)).

### Options

| CMake option | Default | Meaning |
|--------------|---------|---------|
| `QMINIWASM_WITH_AVX512` | ON | Separate TU with `/arch:AVX512` (MSVC) or `-mavx512f -mavx512vl -mavx512bw -mavx512vnni` (GCC/Clang). |
| `QMINIWASM_WITH_NLOPT` | ON | FetchContent NLopt for `optimize_qaoa_p1_cobyla`. Turn OFF if GitHub fetch is blocked. |
| `QMINIWASM_WITH_GRPC` | OFF | Needs Protobuf + gRPC **CONFIG** packages (e.g. vcpkg) and `protobuf::protoc` / `gRPC::grpc_cpp_plugin`. Generates `proto/delta_sync.proto` into `build/proto_gen/`. |
| `QMINIWASM_WITH_NATIVE_RL_RUNTIME` | ON | Build C++ rollout helper for cascade RL/GRPO foundation path. |
| `QMINIWASM_WITH_NATIVE_WASM_HOST` | ON | Build experimental WasmEdge-native bridge C-API helpers. |
| `QMINIWASM_WITH_NATIVE_LOTA_QAF` | ON | Build LOTA forward/merge and t-sign update kernel helpers. |
| `QMINIWASM_WITH_NATIVE_TPEM_BUILDER` | ON | Build native TPEM bundle byte-builder helper. |
| `QMINIWASM_WITH_NATIVE_DQAOA_ROUTING` | ON | Build native routing top-k + cluster assignment helpers. |
| `QMINIWASM_WITH_QUANTUM` | OFF | OpenQASM 3 **subset** lexer/parser + **qutrit** simulator (qubit gates on the \|0>,\|1> subspace) backed by **LibTorch CPU**. Requires `LIBTORCH_ROOT` (same as training). Enables `qmw_openqasm_expval_pauli_z0` / `qmw_routing_trinary_expval_pauli_z0`. |
| `QMINIWASM_WITH_SYCL` | OFF | Builds shared `qminiwasm_sycl` stub; use Intel oneAPI / `-fsycl` for a real `joint_matrix` port. |
| `QMINIWASM_WITH_WASMEDGE` | OFF | Set `WASMEDGE_ROOT` (or `WASMEDGE_INCLUDE_DIR` / `WASMEDGE_LIBRARY`) to the WasmEdge SDK. |
| `QMINIWASM_BUILD_PYBIND` | ON | `qminiwasm_cpp_native` module: `pack_ternary_msb`, `unpack_ternary_msb`, `matvec_best`, CPUID queries. |

### Python module

Build the extension into the build tree and add it to `PYTHONPATH`, or install via your own packaging step. The module name is **`qminiwasm_cpp_native`** (distinct from `qminiwasm._native_ternary` in `ternary_packed/`).

### CPU feature matrix

| Path | Requires |
|------|----------|
| Scalar / AVX2 matvec | Any x86-64 with baseline compile flags. |
| AVX-512 matvec + grammar mask | Runtime `avx512f` (dispatch); TU compiled with AVX-512 F/VL/BW. |
| Non-x86 | `detect_cpu_features()` returns all false; scalar paths only. |

### Security note (gRPC)

TLS and authentication are out of scope for the skeleton service; do not expose `DeltaSync` on untrusted networks without hardening.
