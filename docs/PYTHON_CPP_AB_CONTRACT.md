# Python vs C++ A/B Contract

This document defines the required methodology for Python-to-C++ runtime A/B testing.

## Protocol

- Use identical config, seed, and input corpus for A and B runs.
- Run parity checks before performance/resource checks.
- Use at least 5 repeated trials per scenario and compare median + p90.
- Record implementation mode in output (`python`, `native`, `auto`).
- Record native readiness (`abi_version`, required symbols) and fallback count/reason.

## Required Gates

- **Parity gate**
  - Functional output equality (or bounded numeric tolerance when float kernels are involved).
  - No behavioral regressions in fallback/error semantics.
  - Strict mode (`QMINIWASM_NATIVE_STRICT=1`) fails fast when native prerequisites are missing.
- **Performance gate**
  - Report latency (ms/op) and throughput where applicable.
  - Compare cold and warm paths when caching exists.
- **Resource gate**
  - Report RSS (MiB) and relative CPU trend.
  - Reject regressions over agreed budget unless explicitly waived.

## Environment Matrix

- `QMINIWASM_TERNARY_IMPL=python|native|auto`
- `QMINIWASM_TRIT_PACK_IMPL=python|native|auto`
- `QMINIWASM_MEMORY_ENCODE_IMPL=python|native|auto`
- `QMINIWASM_WASM_EXEC_IMPL=python|native|auto`
- `QMINIWASM_CASCADE_RL_IMPL=python|native|auto`
- `QMINIWASM_TRAINING_RUNTIME_MODE=python|cpp|auto`

## Default Optimized Profile

- Runtime default profile is `optimized_auto` for training and serving bootstrap.
- This means implementation selectors default to `auto` with fallback-safe behavior.
- `QMINIWASM_TPEM_NATIVE_BUNDLE` defaults to `0` unless explicitly overridden.

## Minimum Benchmark Set

- `tests/benchmarks/test_bench_trit_pack.py`
- `tests/benchmarks/test_bench_tpem_bundle_io.py`
- `tests/benchmarks/test_bench_runtime_cache_paths.py`
- Service-level run through `training-wui` local path in both `python` and `cpp` mode.

## Reporting Template

- Scenario and exact command
- A/B env matrix and seed
- Parity result
- Latency delta (%)
- RSS delta (%)
- Decision: pass / fail / needs-waiver

## Rollout Artifact

- Use `docs/NATIVE_ROLLOUT_CHECKLIST.md` for staged promotion (dev/staging/prod), strict canary matrix, and go/no-go checks.
