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
| `QMINIWASM_BUILD_PYBIND` | ON | `qminiwasm_cpp_native` module: `encode_linear_memory_u8`, `pack_ternary_msb`, `unpack_ternary_msb`, `matvec_best`, CPUID queries. |

### WASM linear memory encoding (4096-d)

Authoritative C++ implementation matches Python [`qminiwasm/wasm_host/memory_encode.py`](../qminiwasm/wasm_host/memory_encode.py): [`wasm/linear_memory_encode.hpp`](wasm/linear_memory_encode.hpp) (`qminiwasm::wasm::encode_linear_memory_u8`). Pybind exposes the same layout as **`qminiwasm_cpp_native.encode_linear_memory_u8`**. The older `ternary_packed` extension can still be used by tests that import `qminiwasm._native_ternary`; prefer `qminiwasm_core` for new native/tooling paths.

### Tiered escalation (CGE / QAHR / QPU)

Policy-only C API for multi-hop handoffs: [`escalation/escalation_c_api.h`](escalation/escalation_c_api.h). `qmw_escalation_resolve_next` advances **Tier 1 → 2 → 3** and, depending on `QmwEscalationPolicy` and build flags, toward **native OpenQASM simulation** (`QMINIWASM_WITH_QUANTUM`) or an **external QPU** placeholder tier. `QmwEscalationEnvelopeHeader` prefixes an opaque payload (e.g. WLES bytes). `qmw_escalation_run_native_openqasm_if_applicable` calls [`quantum_bridge_c_api.h`](quantum/quantum_bridge_c_api.h) when the resolved tier is `QMW_ESCALATION_TIER4_NATIVE_QUANTUM_SIM`. Python dict payloads from [`qminiwasm/cognitive/escalation.py`](../qminiwasm/cognitive/escalation.py) remain the canonical training/cloud interchange until a binary spec is extended.

### Expert fleet router (multi-WASM coordinator)

**Inference / fleet** primitive: [`router/expert_fleet_c_api.h`](router/expert_fleet_c_api.h) — `qmw_expert_fleet_topk` and `qmw_expert_fleet_partition` wrap [`qubo/dqaoa_routing_runtime_c_api.h`](qubo/dqaoa_routing_runtime_c_api.h) when `QMINIWASM_WITH_NATIVE_DQAOA_ROUTING=ON`; otherwise they no-op / return zero counts so symbols stay stable for `dlopen`. Vocabulary and **FleetCoordinator vs ExpertMember** roles: [`docs/ENCLAVE_LIFECYCLE.md`](../docs/ENCLAVE_LIFECYCLE.md). This is separate from **`TaxonomyTier::kXpuCluster`**, which tunes **training** throughput (see [`training/taxonomy_tier.hpp`](training/include/qminiwasm/training/taxonomy_tier.hpp)), not how many WASM experts you deploy.

### Host hooks for clustering / routing (experimental)

Stable C callback registry: [`wasm/wasm_host_hooks_c_api.h`](wasm/wasm_host_hooks_c_api.h). Use `qmw_wasm_hooks_set` for optional `on_before_execute` / `on_after_execute` (wired around `qmw_wasmedge_execute` when `QMINIWASM_WITH_NATIVE_WASM_HOST` is ON), `on_linear_memory_snapshot` (fired from `qminiwasm::wles::save_linear_memory` before disk write), and `on_route_hint` (feature vector for [`qmw_expert_fleet_topk`](router/expert_fleet_c_api.h) / [`qmw_route_topk_l2_f64`](qubo/dqaoa_routing_runtime_c_api.h) after the host fills candidates). All callbacks are optional; call from the thread that runs the WASM host unless you add synchronization.

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
