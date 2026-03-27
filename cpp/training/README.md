# C++20 Training Engine Foundation

This module introduces a native training orchestration foundation designed for:

- gRPC-first control and telemetry streaming
- staged, parallel training execution with `std::jthread`
- taxonomy-tier-aware runtime policy selection
- clean C ABI fallback for direct embedding from Go (or other runtimes)

## Build (from `cpp/`)

```bash
cmake -S . -B build -DQMINIWASM_WITH_TRAINING_ENGINE=ON -DQMINIWASM_WITH_GRPC=ON
cmake --build build --target qminiwasm_training_engine_server
```

## Run server

```bash
./build/qminiwasm_training_engine_server 127.0.0.1:50061
```

## Go WUI integration

`training-wui` now uses a generated Go gRPC client for `proto/training_engine.proto`:

1. **Done:** Native `StartTraining` / `StreamTelemetry` / `StopTraining` from the WUI for local and RunPod “train on host” runs.
2. **Done:** WebSocket payloads match the previous `metric` / `alert` contract (`telemetry_source` distinguishes sources).
3. **Partial:** Log-line regex telemetry still applies to **subprocess (Python)** runs only; full parity for quantum/prune/QAOA metrics on the C++ path depends on richer `TelemetryEvent` or synthetic lines.

WUI env: **`QMINIWASM_TRAINING_GRPC_ADDR`** (default `127.0.0.1:50061`). See [`training-wui/README.md`](../../training-wui/README.md).
