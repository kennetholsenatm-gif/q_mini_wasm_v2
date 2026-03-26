# WasmEdge WASI-NN plugin (stub)

Optional Rust crate layout for delegating ternary tensor ops to **WebNN** or **DirectML**
via WasmEdge’s WASI-NN plugin interface.

## Layout

- ``wasmedge-ternary-nn/Cargo.toml`` — ``cdylib`` crate name ``wasmedge_plugin_ternary_nn``.
- Implement the WasmEdge plugin ABI that registers graph builders and inference
  entry points; map ``DOT_UINT8_TERNARY``-style ops to host runtimes.

## Primary path in this repo

The Python training stack uses **wasmtime** with host imports
(``qminiwasm.wasm_host.host_tensor``) first; this crate is for production WasmEdge
deployments without PyTorch on the edge host.
