# Layout realignment RFC (Phase 1 pilot)

The master realignment prompt calls for **flattening** deep trees (`engine/`, `qminiwasm/`, etc.) into top-level **domains** (memory packing, WASM runtime, quantum routing). Full moves are high-churn: every import, packaging entry point, and CI path must move together.

## Principles

1. **No big-bang renames** — ship **compatibility shims** and **additive** config (new TOML tables / env aliases) before moving directories.
2. **Pilot first** — each pilot should be **mechanical** (moves + re-exports) with a **short** rollback path.
3. **Engine kwargs stay stable** — `EngineConfig` and `run_training_loop` still expose `checkpoint_*` names until a dedicated deprecation release; new **TOML** surface uses edge vocabulary first.

## Pilot 1 (landed): `[tpem]` training table

- **What:** Optional `[tpem]` table in training TOML, same keys as the `[checkpoint]` persistence table (`load_path`, `save_path`, `best_path`, `latest_path`).
- **Merge rule:** For each key, if `[tpem]` sets a non-null value, it **overrides** `[checkpoint]` for that key when building `EngineConfig` (see `qminiwasm.engine.training_schema.TrainingConfig.to_engine_kwargs`).
- **Serve:** `[serve]` accepts optional `tpem = "path.pt"`; it is preferred over **`checkpoint`** when both are set (Go **`qmw-serve`** / WUI consume **`serve.toml`**).

## Pilot 2 (landed): `qminiwasm.tpem` package

- **What:** Canonical implementation in [`qminiwasm/tpem/trainable_tpem.py`](../qminiwasm/tpem/trainable_tpem.py); [`qminiwasm/tpem/__init__.py`](../qminiwasm/tpem/__init__.py) re-exports the public API.
- **Compatibility:** [`qminiwasm/training/trainable_tpem.py`](../qminiwasm/training/trainable_tpem.py) and [`qminiwasm/training/checkpoint.py`](../qminiwasm/training/checkpoint.py) remain thin re-exports.

## Pilot 3 (landed): `qminiwasm.wasm_host`

- **What:** Canonical WASM host runtime lives in [`qminiwasm/wasm_host/`](../qminiwasm/wasm_host/). Older **`qminiwasm.enclave`** import paths were removed from the tree after an in-repo migration and deprecation window; use **`qminiwasm.wasm_host`** only.
- **`qminiwasm.wasm`:** Package-level alias only (re-exports `wasm_host`); submodule shims were removed — import **`qminiwasm.wasm_host.<submodule>`** when you need a submodule path.
- **CI:** `python -m qminiwasm.wasm_host.tpem_bundle verify …`

## Pilot 4 (landed): `qminiwasm.engine` only

- **What:** Training TOML, `EngineConfig`, `train.main`, `training_schema`, and helpers live in [`qminiwasm/engine/`](../qminiwasm/engine/). The old top-level **`engine/`** package was **removed**. **Operator training** uses **Go + C++ gRPC** ([TRAINING_NATIVE_PARITY.md](TRAINING_NATIVE_PARITY.md)); Python **`train.main`** remains for tests and internal tooling only.
- **Boundary:** [ENGINE_QMINIWASM_BOUNDARY.md](ENGINE_QMINIWASM_BOUNDARY.md): **`qminiwasm.engine` → library** only; library modules must not import `qminiwasm.engine`.

## Pilot 5 (landed): removed `qminiwasm.enclave`

- **What:** The compatibility package **`qminiwasm.enclave`** was deleted from this repository. Downstream code must import **`qminiwasm.wasm_host`** (or submodules such as `qminiwasm.wasm_host.memory_encode`).
- **Tests:** [`tests/test_wasm_host_layout.py`](../tests/test_wasm_host_layout.py) checks `wasm_host` + `tpem` layout only.

## Pilot 6 (landed): `qminiwasm.cli` graph commands

- **What:** [`qminiwasm/cli/__main__.py`](../qminiwasm/cli/__main__.py) — **`python -m qminiwasm.cli graph validate|apply`** for Enclave-as-Code manifests.

## Next pilots

| Pilot | Idea | Risk |
|-------|------|------|
| — | Optional removal of **`qminiwasm.wasm`** / **`qminiwasm.state`** package aliases after downstream uses only **`wasm_host`** | Low |

## References

- Status: [MASTER_REALIGNMENT_STATUS.md](MASTER_REALIGNMENT_STATUS.md)
- `qminiwasm.engine` / library boundary: [ENGINE_QMINIWASM_BOUNDARY.md](ENGINE_QMINIWASM_BOUNDARY.md); implementation: [`qminiwasm/engine/`](../qminiwasm/engine/)
- CLI: [`qminiwasm/cli/`](../qminiwasm/cli/) (`python -m qminiwasm.cli graph …`)
- Schema sample: [configs/training/schema.toml](../configs/training/schema.toml)
