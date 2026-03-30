# `qminiwasm.engine` vs library boundary

This document tracks **Pilot 4** in [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md): where training orchestration types live and how they may import the core library.

## Roles

| Location | Responsibility |
|----------|------------------|
| **`qminiwasm/`** (library) | Model, training loop, WASM host, data pipeline, quantum/fabric, shared config types. Keeps notebooks and unit tests free of heavy TOML glue where possible. |
| **`qminiwasm.engine`** | **TOML/schema/config layer**: training TOML → `EngineConfig`, `train.main()` for tests and tooling, `training_schema`, secret sanitization, native CLI argv helper — under [`qminiwasm/engine/`](../qminiwasm/engine/). |

There is **no** top-level `engine/` Python package anymore. **Operator training** uses the Training WUI and **`qmw-grpc-train`** (see [TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)).

## Dependency rule

- **`qminiwasm.engine` → `qminiwasm` (library)**: allowed and expected.
- **Library internals → `qminiwasm.engine`**: **forbidden** (avoids import cycles and keeps `import qminiwasm` light).

## Inventory: what `qminiwasm.engine` imports from the library

| Module | Imports from `qminiwasm` |
|--------|-------------------------|
| `qminiwasm/engine/train.py` | `qminiwasm.training.loop.run_training_loop` |
| `qminiwasm/engine/config.py` | Lazy: `qminiwasm.wasm_host.engine` for `wasm_runtime_kwargs()` |
| `qminiwasm/engine/training_schema.py` | Doc references only (`WasmRuntimeConfig`) |

## Entrypoints

- **Training (operator):** Training WUI + C++ `TrainingEngineService` gRPC, or **`go run ./cmd/qmw-grpc-train`** from **`training-wui/`** ([TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)).
- **Training (library/tests):** call **`qminiwasm.engine.train.main()`** with an **`EngineConfig`** (no `python -m` module).
- **Graph manifest CLI:** **`python -m qminiwasm.cli graph …`**
- **Inference HTTP:** Go **`qmw-serve`** and WUI **`/api/serve/*`** (embedded Go server).

## WASM runtime

Use **`qminiwasm.wasm_host`** for Wasmtime, memory encoding, trit packing, WASI, TPEM bundle helpers. **`qminiwasm.wasm`** remains a **package-level** alias of `wasm_host` only (no per-submodule shims — import `qminiwasm.wasm_host.*` for submodules). **`qminiwasm.state`** re-exports delta compression from `wasm_host` for older doc references.

The canonical **4096-d linear-memory float encoding** for training is implemented in Python in `wasm_host.memory_encode` and duplicated for speed in native code under [`cpp/wasm/linear_memory_encode.hpp`](../cpp/wasm/linear_memory_encode.hpp) (also exposed as `qminiwasm_cpp_native.encode_linear_memory_u8` when the CMake pybind target is built). For **multi-instance routing and future WASM clustering**, the repo defines an experimental C hook ABI in [`cpp/wasm/wasm_host_hooks_c_api.h`](../cpp/wasm/wasm_host_hooks_c_api.h) (snapshots on WLES save, execute pre/post around the WasmEdge bridge stub, and an optional route-hint channel); pairing those hooks with native `qmw_route_*` helpers remains operator-specific.

**Certainty-gated escalation tiers** in native code: [`cpp/escalation/escalation_c_api.h`](../cpp/escalation/escalation_c_api.h) (`qmw_escalation_resolve_next`, envelope header, optional `qmw_escalation_run_native_openqasm_if_applicable`) complements Python CGE payloads in `qminiwasm.cognitive.escalation` and does not replace them in the default training stack.

**Expert fleet (multi-WASM router):** [`cpp/router/expert_fleet_c_api.h`](../cpp/router/expert_fleet_c_api.h) wraps routing top-k / partition primitives for coordinator-style deployments; vocabulary is spelled out in [`docs/ENCLAVE_LIFECYCLE.md`](ENCLAVE_LIFECYCLE.md) and ties to [`cpp/wasm/wasm_host_hooks_c_api.h`](../cpp/wasm/wasm_host_hooks_c_api.h) `on_route_hint`.

## Related docs

- [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md)
- [MASTER_REALIGNMENT_STATUS.md](MASTER_REALIGNMENT_STATUS.md)
