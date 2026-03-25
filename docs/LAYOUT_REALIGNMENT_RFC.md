# Layout realignment RFC (Phase 1 pilot)

The master realignment prompt calls for **flattening** deep trees (`engine/`, `qminiwasm/`, etc.) into top-level **domains** (memory packing, WASM runtime, quantum routing). Full moves are high-churn: every import, packaging entry point, and CI path must move together.

## Principles

1. **No big-bang renames** — ship **compatibility shims** and **additive** config (new TOML tables / env aliases) before moving directories.
2. **Pilot first** — each pilot should be **mechanical** (moves + re-exports) with a **short** rollback path.
3. **Engine kwargs stay stable** — `EngineConfig` and `run_training_loop` still expose `checkpoint_*` names until a dedicated deprecation release; new **TOML** surface uses edge vocabulary first.

## Pilot 1 (landed): `[tpem]` training table

- **What:** Optional `[tpem]` table in training TOML, same keys as the legacy persistence table (`load_path`, `save_path`, `best_path`, `latest_path`).
- **Merge rule:** For each key, if `[tpem]` sets a non-null value, it **overrides** the legacy table for that key when building `EngineConfig` (see `engine.training_schema.TrainingConfig.to_engine_kwargs`).
- **Serve:** `[serve]` accepts optional `tpem = "path.pt"`; it is preferred over the legacy weight path when both are present (`engine.serve.get_model`).

## Pilot 2 (landed): `qminiwasm.tpem` package

- **What:** Canonical implementation in [`qminiwasm/tpem/trainable_tpem.py`](../qminiwasm/tpem/trainable_tpem.py); [`qminiwasm/tpem/__init__.py`](../qminiwasm/tpem/__init__.py) re-exports the public API.
- **Compatibility:** [`qminiwasm/training/trainable_tpem.py`](../qminiwasm/training/trainable_tpem.py) and [`qminiwasm/training/checkpoint.py`](../qminiwasm/training/checkpoint.py) remain thin re-exports.

## Next pilots (not started)

| Pilot | Idea | Risk |
|-------|------|------|
| P3 | Top-level `wasm_runtime/` or `edge/` for enclave + WASM (vs nested `qminiwasm/enclave/`) | High |
| P4 | Retire `engine/` vs `qminiwasm/` split after boundary doc | High |

## References

- Status: [MASTER_REALIGNMENT_STATUS.md](MASTER_REALIGNMENT_STATUS.md)
- Schema sample: [configs/training/schema.toml](../configs/training/schema.toml)
