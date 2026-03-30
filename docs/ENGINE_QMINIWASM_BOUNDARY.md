# Native runtime boundary (Go / C++ / WASM)

This document supersedes the former “`qminiwasm.engine` vs library” split. **Operator workflows** do not use an interpreter package: they use **Go** (WUI, gRPC clients, edge artifact builders) and **C++** (LibTorch engine, WASM encode, hooks, escalation, expert fleet).

Historical notes about a Python schema layer live only in git history; new work belongs under [`training-wui/`](../training-wui/) and [`cpp/`](../cpp/). Tracking: [DEPYTHONIZATION.md](DEPYTHONIZATION.md), [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md).

## Roles

| Location | Responsibility |
|----------|----------------|
| **`training-wui/`** (Go) | TOML validation, gRPC runner (`qmw-grpc-train`), WUI API, env doc generation, infer HTTP stub until C++ bridge lands. |
| **`cpp/`** (C++) | `TrainingEngineService`, LibTorch `CoreModule`, [`linear_memory_encode.hpp`](../cpp/wasm/linear_memory_encode.hpp), [`wasm_host_hooks_c_api.h`](../cpp/wasm/wasm_host_hooks_c_api.h), [`escalation_c_api.h`](../cpp/escalation/escalation_c_api.h), [`expert_fleet_c_api.h`](../cpp/router/expert_fleet_c_api.h). |

## Entrypoints (authoritative)

- **Training (operator):** Training WUI + C++ `TrainingEngineService` gRPC, or `go run` **`./cmd/qmw-grpc-train`** from **`training-wui/`** ([TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)).
- **Inference HTTP:** Go **`qmw-serve`** and WUI **`/api/serve/*`** — extend with **C++ LibTorch RPC** per [training-wui/infer_serve.go](../training-wui/infer_serve.go).

## WASM encoding and hooks

Canonical **4096-d** (configurable width) linear-memory → float encoding: **native** [`cpp/wasm/linear_memory_encode.hpp`](../cpp/wasm/linear_memory_encode.hpp) (`encode_linear_memory_u8`).

**Multi-instance routing:** [`wasm_host_hooks_c_api.h`](../cpp/wasm/wasm_host_hooks_c_api.h) (`on_route_hint`, snapshots, pre/post execute around the Wasmedge test bridge).

**Escalation:** [`escalation_c_api.h`](../cpp/escalation/escalation_c_api.h).

**Expert fleet:** [`expert_fleet_c_api.h`](../cpp/router/expert_fleet_c_api.h) + [ENCLAVE_LIFECYCLE.md](ENCLAVE_LIFECYCLE.md).

## Related docs

- [TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)
- [MASTER_REALIGNMENT_STATUS.md](MASTER_REALIGNMENT_STATUS.md)
- [DEPYTHONIZATION.md](DEPYTHONIZATION.md)
