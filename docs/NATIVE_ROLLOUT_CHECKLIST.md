# Native Rollout Checklist (Compact)

This artifact captures a staged rollout plan using the latest native pipeline outputs from `scripts/run_python_cpp_ab.py`.

## Snapshot (Current Evidence)

- Run status: `python`, `native`, `auto` all gate-pass.
- Native readiness snapshot:
  - `ctypes_lib_loaded=false`
  - `pybind_loaded=true`
  - `pybind_symbols`: `dot_u8_i8`, `pack_ternary_list`, `unpack_ternary_list`, `encode_linear_memory_u8`
  - fallback count observed in run: `0`
- Key deltas vs `python` baseline:
  - `pack_ns_per_trit`: `~+55.97%` faster (`native`)
  - `unpack_ns_per_trit`: `~+35.05%` faster (`native`)
  - `memory_encode_us_per_call`: `~+96.87%` faster (`native`)
  - `state_store_us`: `~5.42%` slower (`native`)
  - `state_lookup_us`: `~7.07%` slower (`native`)

## Gate Checklist

## Dev (default-on, fallback-safe)

- [ ] `python scripts/run_python_cpp_ab.py` passes in all modes.
- [ ] `fallback_count == 0` for normal local run; any fallback includes explicit reason code.
- [ ] `python -m pytest tests/test_ab_runtime_toggles.py -q` passes.
- [ ] `go test ./...` in `training-wui` passes.
- [ ] WUI local start in `auto` mode routes deterministically (native if reachable, else python).

## Staging (strict gate enabled)

- [ ] Enable strict canary matrix (below) for CI and one staging node.
- [ ] Confirm strict runs fail fast if required native prerequisites are missing.
- [ ] Confirm strict runs pass when native prerequisites are present.
- [ ] Validate serving `/health` and training telemetry emit mode/fallback details.
- [ ] No silent downgrade from strict-native intent.

## Production (auto-fallback + strict canary lane)

- [ ] Keep user traffic on `auto` defaults.
- [ ] Keep strict canary jobs scheduled (non-blocking for user traffic).
- [ ] Alert on fallback reason spikes and native readiness regressions.
- [ ] Block promotion if parity fails or unexplained fallback volume increases.

## Recommended Strict Canary Env Matrix

Use these lanes as a minimum matrix.

| Lane | Purpose | Core env |
|---|---|---|
| `auto-default` | Production-safe behavior with deterministic fallback | `QMINIWASM_TRAINING_RUNTIME_MODE=auto`, `QMINIWASM_TERNARY_IMPL=auto`, `QMINIWASM_TRIT_PACK_IMPL=auto`, `QMINIWASM_MEMORY_ENCODE_IMPL=auto`, `QMINIWASM_WASM_EXEC_IMPL=auto`, `QMINIWASM_CASCADE_RL_IMPL=auto`, `QMINIWASM_TPEM_NATIVE_BUNDLE=0`, `QMINIWASM_NATIVE_STRICT=0` |
| `strict-kernels` | Enforce native kernel hotpaths | `QMINIWASM_NATIVE_STRICT=1`, `QMINIWASM_TERNARY_IMPL=native`, `QMINIWASM_TRIT_PACK_IMPL=native`, `QMINIWASM_MEMORY_ENCODE_IMPL=native`, `QMINIWASM_CASCADE_RL_IMPL=native`, `QMINIWASM_TPEM_NATIVE_BUNDLE=0` |
| `strict-wasm` | Enforce native wasm host path correctness | `QMINIWASM_NATIVE_STRICT=1`, `QMINIWASM_WASM_EXEC_IMPL=native`, `QMINIWASM_TRAINING_RUNTIME_MODE=cpp` |
| `python-baseline` | Regression control baseline | `QMINIWASM_NATIVE_STRICT=0`, `QMINIWASM_TERNARY_IMPL=python`, `QMINIWASM_TRIT_PACK_IMPL=python`, `QMINIWASM_MEMORY_ENCODE_IMPL=python`, `QMINIWASM_WASM_EXEC_IMPL=python`, `QMINIWASM_CASCADE_RL_IMPL=python`, `QMINIWASM_TRAINING_RUNTIME_MODE=python` |

## Promotion Criteria

- Promote Dev -> Staging only if:
  - all parity tests pass,
  - strict lanes behave fail-fast on missing native prereqs,
  - no silent fallback path observed.
- Promote Staging -> Prod only if:
  - A/B report remains performance-positive on hotpaths,
  - fallback reasons are explicit and within expected envelope,
  - strict canary lane passes for at least 3 consecutive runs.

