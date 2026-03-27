# Process environment: CI, containers, and tooling (not WUI configuration)

**Audience:** maintainers, CI, Docker images, serverless handlers, and local **toolchains** — not operators using the **Training WUI**.

**Policy:** End users configure training and inference through **`configs/training/*.toml`**, **`configs/serve/*.toml`**, and the WUI. The variables below are **not** listed in [`.env.example`](../.env.example) or [`.env.schema`](../.env.schema) because they are **not** “put secrets in `.env`” workflow — they exist for automation, strict CI gates, native build paths, or deployment injection.

When a knob has a TOML equivalent, **prefer TOML** (e.g. `[wasm]` in training config). This document is the inventory of what the codebase may still **read** from `os.environ`.

## Training engine (`qminiwasm.engine` / `run_training_loop`)

| Variable | Role |
|----------|------|
| `QMINIWASM_STRICT_CONFIG_VALIDATION` | Strict validation of config values (tests / CI). |
| `QMINIWASM_STRICT_XPU` | Fail if XPU requested but unavailable. |
| `QMINIWASM_STRICT_ENCLAVE_FOOTPRINT` | Strict enclave footprint gate. |
| `QMINIWASM_ASSERT_ZERO_MOCK_RATIO` | Training gate: no mock WASM samples. |
| `QMINIWASM_TPEM_LATEST_EVERY_N_EPOCHS` | Checkpoint cadence override. |
| `QMINIWASM_ENCLAVE_ADAPTER` | Enable experimental enclave adapter backend. |
| `QMINIWASM_NATIVE_RL_RUNTIME` | Native cascade rollout path. |
| `ACCELERATOR` | Legacy global accelerator hint (prefer `[hardware].accelerator` in TOML). |

**Removed from env (use TOML only):** `QMINIWASM_DATALOADER_NUM_WORKERS` — use `[training].dataloader_num_workers`. WASM store limits / backend / Memory64 — use `[wasm]` and `[enclave]` in TOML (`EngineConfig` no longer applies `QMW_WASM_*` / `QMINIWASM_WASM_BACKEND` / `QMINIWASM_USE_MEMORY64` / `QMINIWASM_WASM_MEMORY64_MAX_MB` overrides).

## WASM host / curriculum build (`qminiwasm.wasm_host`)

| Variable | Role |
|----------|------|
| `QMINIWASM_WASM_EXEC_IMPL` | `auto` / `python` / `native` execution path. |
| `QMINIWASM_WASM_RUNTIME_WASM_OPT` | wasm-opt in runtime pipeline. |
| `QMINIWASM_WASM_RUNTIME_WASM_OPT_LEVEL` | wasm-opt level. |
| `QMINIWASM_WASM_C_LINK` | `bare` vs `wasip1` C link mode. |
| `QMINIWASM_WASM_CLANG_PROFILE` | Clang profile for WASM compile. |
| `QMINIWASM_WASM_EXPORT_MODE` | WASM export mode. |
| `WASI_SDK_PATH` | Path to WASI SDK (toolchain). |

## Runtime mode defaults (`qminiwasm.runtime_modes`)

Maps like `QMINIWASM_TERNARY_IMPL`, `QMINIWASM_MEMORY_ENCODE_IMPL`, `QMINIWASM_CASCADE_RL_IMPL`, etc. — used for A/B and CI matrices; prefer documented TOML where exposed.

## Model / router / config (`qminiwasm.model`, `qminiwasm.fabric.router`, `qminiwasm.config`)

Various toggles (`QMW_DISABLE_TROPICAL_ATTN`, `QMW_ROUTING_LATENCY_BUDGET_MS`, `ENCLAVE_TIER`, `N_MAX_LOOPS`, …) — infrastructure and experiments; not part of the WUI `.env` contract.

## Training WUI (`training-wui`)

Preflight and subprocess code may pass **secrets** (IBM token, quantum backend name) into a Python probe; **training** still comes from the selected TOML file. Internal keys like `QMW_HF_*` / `QMW_FACTS_*` are script helpers, not user configuration.

## Serverless / containers

| Variable | Role |
|----------|------|
| `QMW_REPO_ROOT` | Repo root inside container. |
| `PYTHON_BIN` | Python executable for child processes. |

## Native build / setup

| Variable | Role |
|----------|------|
| `QMINIWASM_BUILD_NATIVE` | Build native extensions. |
| `VCPKG_ROOT`, `CMAKE_TOOLCHAIN_FILE` | C++ gRPC training engine build. |
| `QMINIWASM_TRAINING_GRPC_ADDR` | gRPC client address for C++ engine. |

## CI workflows

`.github/workflows/ci.yml` may set `QMINIWASM_WASM_FALLBACK_POLICY=error` for strict WASM behavior. Prefer aligning CI with TOML fixtures where possible.

---

**Credentials** (Hub, IBM, RunPod) stay in `.env` — see [environment-variables.md](environment-variables.md).
