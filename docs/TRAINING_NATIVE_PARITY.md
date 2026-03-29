# Training: native (Go + C++) vs legacy Python loop

Production training from the Training WUI uses **only** the C++ `TrainingEngineService` over gRPC (plus `cmd/qmw-grpc-train` on RunPod SSH hosts). The historical `python -m qminiwasm.engine` path has been removed from the WUI.

## Parity matrix (high level)

| Area | Native (C++ gRPC + Go WUI) | Python `qminiwasm` package |
|------|----------------------------|----------------------------|
| TOML → `TrainingConfig` | [training-wui/trainingconfig](../training-wui/trainingconfig/config.go) | `EngineConfig` / `run_training_loop` (wider surface) |
| Cascade curriculum loop | Supported when mapped in proto / C++ | Full |
| HF tabular / datasets | Mapped via `HfDatasetParams` + C++ fetch | `datasets`, custom loaders |
| Quantum (Qiskit / PennyLane) | Not in C++ engine | Optional sidecar / `sidecar/quantum/` |
| WASM runtime toggles in training | Native engine + TOML env to C++ | `wasmtime` / full host |
| Inference HTTP | Go `qmw-serve` + embedded stub in WUI (`/api/serve/*`) | `uvicorn` + FastAPI (legacy) |

Extend this table when adding proto fields or C++ features so Mission Control and docs stay honest.

## Operational requirements

- Local: run `qminiwasm_training_engine_server` on `[wui] grpc_addr` from `configs/wui.toml` (see `scripts/start-training-stack.ps1`).
- RunPod (train on pod): pod needs **Go**, **CMake-built** `qminiwasm_training_engine_server`, and **LibTorch** on `LD_LIBRARY_PATH` (see `training-wui/runpod_remote.go`).
