# `qminiwasm.engine` vs library boundary

This document tracks **Pilot 4** in [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md): where training/serve orchestration lives and how it may import the core library.

## Roles

| Location | Responsibility |
|----------|------------------|
| **`qminiwasm/`** (library) | Model, training loop, WASM host, data pipeline, quantum/fabric, shared config types. Keeps notebooks and unit tests free of the TOML/CLI process layer. |
| **`qminiwasm.engine`** | **Process shell**: `.env` (credentials), training TOML → `EngineConfig`, training `main`, FastAPI inference (`serve`), secret sanitization — under [`qminiwasm/engine/`](../qminiwasm/engine/). |

There is **no** top-level `engine/` Python package anymore; use **`python -m qminiwasm.engine`** (or **`python -m qminiwasm.cli train`**).

## Dependency rule

- **`qminiwasm.engine` → `qminiwasm` (library)**: allowed and expected.
- **Library internals → `qminiwasm.engine`**: **forbidden** (avoids import cycles and keeps `import qminiwasm` light).

## Inventory: what `qminiwasm.engine` imports from the library

| Module | Imports from `qminiwasm` |
|--------|-------------------------|
| `qminiwasm/engine/train.py` | `qminiwasm.training.loop.run_training_loop` |
| `qminiwasm/engine/serve.py` | `qminiwasm.config`, `qminiwasm.wasm_host.engine.WasmRuntimeConfig`; lazy imports for `get_device`, `QMiniWASM` |
| `qminiwasm/engine/config.py` | Lazy: `qminiwasm.wasm_host.engine` for `wasm_runtime_kwargs()` |
| `qminiwasm/engine/training_schema.py` | Doc references only (`WasmRuntimeConfig`) |

## Entrypoints

- **Training:** `python -m qminiwasm.engine [--config PATH]` or `python -m qminiwasm.cli train …` (**legacy direct CLI**; the **Training WUI** default does not invoke these — it uses C++ gRPC per [TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md).)
- **Inference:** `uvicorn qminiwasm.engine.serve:app` (**legacy FastAPI** HTTP server)

## WASM runtime

Use **`qminiwasm.wasm_host`** for Wasmtime, memory encoding, trit packing, WASI, TPEM bundle helpers. **`qminiwasm.wasm`** remains a **package-level** alias of `wasm_host` only (no per-submodule shims — import `qminiwasm.wasm_host.*` for submodules). **`qminiwasm.state`** re-exports delta compression from `wasm_host` for legacy docs.

## Related docs

- [LAYOUT_REALIGNMENT_RFC.md](LAYOUT_REALIGNMENT_RFC.md)
- [MASTER_REALIGNMENT_STATUS.md](MASTER_REALIGNMENT_STATUS.md)
