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

**Source of truth:** **`configs/runtime.toml`**. When a variable is **unset** in the process environment, `qminiwasm.runtime_modes` reads the matching table (`[runtime.impl]`, `[training]`, `[fabric]`, `[hardware]`, `[loader]`, `[native]`).

CI and A/B scripts may still **export** `QMINIWASM_*` / `QMW_*` / `SYCL_BACKEND` so non-empty values override the file (same keys as before: ternary/trit/memory/wasm/cascade impls, native strict, routing budget, etc.).

## Model / router / config (`qminiwasm.model`, `qminiwasm.fabric.router`, `qminiwasm.config`)

Various toggles (`QMW_DISABLE_TROPICAL_ATTN`, `QMW_ROUTING_LATENCY_BUDGET_MS`, `ENCLAVE_TIER`, `N_MAX_LOOPS`, …) — infrastructure and experiments; not part of the WUI `.env` contract.

## Training WUI (`training-wui`)

**Operator config** for engine mode, gRPC address, RunPod SSH defaults, and runtime-profile mirrors lives in **`configs/wui.toml`** (and optional **`-wui-config` / `-training-runtime` / `-grpc-addr`** flags). The repo default is **`training_runtime_mode = "native"`** (C++ gRPC). The WUI injects matching **`QMINIWASM_*`** vars into Python children so `qminiwasm.engine` stays aligned without relying on the shell environment.

Preflight runs a **Python torch/SYCL/IBM probe** when available; if it fails or returns invalid JSON, the WUI falls back to a **Go-only** preflight (TOML + gRPC reachability, no live IBM or torch device). Set **`QMW_WUI_PREFLIGHT_GO_ONLY=1`** on the WUI process to skip the Python probe entirely (hosts without the Python training stack). Subprocess code may pass **secrets** into the Python probe; **training** still comes from the selected TOML file. Internal keys like `QMW_HF_*` / `QMW_FACTS_*` are script helpers, not user configuration.

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
| `QMINIWASM_TRAINING_GRPC_ADDR` | Injected by WUI from **`configs/wui.toml`**; CI may still set it when driving **`qminiwasm.engine`** without the WUI. |

## CI workflows

`.github/workflows/ci.yml` may set `QMINIWASM_WASM_FALLBACK_POLICY=error` for strict WASM behavior. Prefer aligning CI with TOML fixtures where possible.

### Pull request targets and required checks

Core workflows (`.github/workflows/ci.yml`, `security-scans.yml`, `semgrep.yml`, `codeql.yml`) run on **`pull_request`** when the PR **base** branch is **`main`** or matches **`pr/**`** (e.g. `pr/native-grpc-training-wui`). That way **stacked PRs** (fix branch → integration branch → `main`) still execute the same **lint-and-test**, **build-artifacts**, **training-wui (Go)**, and security jobs that branch protection expects.

If you open a PR whose **base** is something else (for example a personal fork default branch) and those workflows never appear, either retarget the PR to `main` or to a branch under `pr/` that you intend to merge through, or add the base pattern to the workflow `on.pull_request.branches` list.

Workflow files must be present on the **default branch** for GitHub to schedule runs reliably; after changing triggers, merge that update to `main` first, then push or **Reopen** the stacked PR so Actions picks up the new configuration.

---

**Credentials** (Hub, IBM, RunPod) stay in `.env` — see [environment-variables.md](environment-variables.md).
