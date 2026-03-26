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

## Deferred Go WUI integration

Recommended incremental integration path in `training-wui`:

1. Add a local Go gRPC client for `training_engine.proto`.
2. Keep current browser websocket contract unchanged.
3. Bridge typed gRPC telemetry events into existing websocket payloads.
4. Remove regex log parsing when parity is reached.
