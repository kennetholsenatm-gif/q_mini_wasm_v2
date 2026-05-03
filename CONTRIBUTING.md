# Contributing and cloning

This repo is meant to clone cleanly: **large corpora, weights, and CMake outputs stay out of git** (see `.gitignore`). Use external disk for data when training.

## Prerequisites

- **Go** (1.21+ recommended) with **CGO enabled** on Windows (MSVC toolchain so the host can load `q_training.dll`).
- **CMake** + **Intel oneAPI DPC++/C++** (`icx` / `icpx` / `icx-cl`) with **Ninja** or **Visual Studio** using the **IntelLLVM** toolset (`-T IntelLLVM`). Native `q_training` / core **do not support** plain MSVC or non-SYCL configures.
- **Routing policy guard**: from the repo root, `python scripts/check_no_sycl_route_off.py .` must succeed before merging native/training/router changes. CI runs it automatically; `ctest` includes **`qmini_policy_no_sycl_route_off`** when tests are enabled. It blocks reintroducing the deleted SYCL routing **Off** enum variant and the deleted TOML value for CPU-only symplectic routing.
- **Throughput sizing — no hidden caps** (see `.clinerules` section `[THROUGHPUT_SIZING_NO_HIDDEN_CAPS]`): do not add repo-fixed ceilings on multi-row MoE / batched FF chunk sizing; use `training.*` TOML and InitSession. From the repo root, `python scripts/check_no_throughput_hidden_caps.py .` must succeed; CI and `ctest` include **`qmini_policy_no_throughput_hidden_caps`**.

## Clone

```powershell
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2
```

## Build (minimal path for the WUI + training host)

1. Configure and build the training DLL from an **Intel oneAPI** environment (so `icx` and linker libs such as `libircmt.lib` resolve). Prefer the repo script, or Ninja + `icx` explicitly:

   ```powershell
   # Recommended (finds icx-cl + Ninja, Release, SYCL):
   powershell -NoProfile -File .\scripts\build_qminiwasm.ps1

   # Manual equivalent (paths vary by oneAPI / VS install):
   cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -G Ninja `
     -DCMAKE_BUILD_TYPE=Release `
     -DCMAKE_CXX_COMPILER="C:\Program Files (x86)\Intel\oneAPI\compiler\latest\bin\icx.exe" `
     -DCMAKE_CXX_FLAGS=-fsycl -DCMAKE_SHARED_LINKER_FLAGS=-fsycl -DCMAKE_EXE_LINKER_FLAGS=-fsycl
   cmake --build q_mini_wasm_v2/build_sycl --target q_training
   ```

   Prefer **`scripts/build_qminiwasm.ps1`**: after linking **`q_training`**, it always runs **`cmake -P`** on **`q_mini_wasm_v2/cmake/post_copy_training_dll.cmake`** (publish does not depend on Ninja skipping a no-op **`POST_BUILD`**) and on **`post_copy_vcpkg_curl_runtime_dlls.cmake`** when **`VCPKG_ROOT`** is set or a default vcpkg tree contains **`installed/x64-windows/bin/libcurl.dll`**. Optional CMake target **`q_training_publish`** still runs the same training DLL copy for manual builds.

   Do **not** point `-B` at an old tree configured with MSVC only; native CMake strips stale `USE_SYCL` cache entries — targets always compile with **`USE_SYCL=1`**.

2. Build the Go server **at the repository root** (this is the only user-facing executable):

   ```powershell
   cd C:\GitHub\q_mini_wasm_v2   # or your clone path
   go build -o qminiwasm.exe ./cmd/qminiwasm
   ```

   Or: `powershell -NoProfile -File .\scripts\build_qminiwasm.ps1`

3. **Layout** is defined in [`config/path_map.toml`](config/path_map.toml): canonical **`qminiwasm.exe`** at the repo root, **`wui/`** beside it, and **`q_training.dll`** next to the exe (CGO load order). **`scripts/build_qminiwasm.ps1`** publishes **`q_training.dll`** to the repo root and **`native_runtime/`** via **`cmake -P post_copy_training_dll.cmake`** (always, after link). vcpkg **curl** (and peers) land beside that DLL via **`post_copy_vcpkg_curl_runtime_dlls.cmake`** when vcpkg is discoverable. The CMake target **`q_training_publish`** is an alternate one-step training DLL copy. If the root DLL is **locked** by a running `qminiwasm.exe`, the **`native_runtime`** copy may still update—stop the server, then copy **`native_runtime\q_training.dll`** over the root copy.

## Data and config (not in git)

The Go host defaults to **`C:\q_mini_data`** for runtime files (`DataDir` in `cmd/qminiwasm/main.go`). Set **`QMINI_DATA_DIR`** to redirect the whole tree (config, datasets, checkpoints). **`QMINI_TRAINING_CONFIG`** can point at any absolute `.toml` (e.g. your repo copy) without moving the rest of the tree. The WUI shows the **active training TOML path** on load; editing only `config/training_config.toml` in git does nothing until that file is copied to the active path or you set the env vars.

Create:

- `C:\q_mini_data\config\training_config.toml` — copy from [config/training_config.toml](config/training_config.toml) in the repo as a starting point.
- `C:\q_mini_data\config\data_sources.toml` — copy from [config/data_sources.toml](config/data_sources.toml).
- `C:\q_mini_data\datasets\` — put `.txt` / `.jsonl` training files here (or set `paths.dataset_dir` in `training_config.toml`).

**Do not commit** downloaded books, weights, or local `build_*` trees; they are ignored by design. Use a dedicated SYCL build directory (e.g. `build_sycl`) so you never confuse it with an obsolete non-SYCL cache.

## What to commit

- Source under `q_mini_wasm_v2/`, `cmd/qminiwasm/`, `config/` (templates), `wui/`, docs you want versioned (e.g. `q_mini_docs/`).
- Avoid committing `*.exe`, `*.dll`, logs, CSV audit dumps, and extracted release assets.

Throughput, CPU vs GPU expectations, and **`training.timing_to_stderr`** in TOML: see **[q_mini_docs/TRAINING_THROUGHPUT.md](q_mini_docs/TRAINING_THROUGHPUT.md)**. Quick CPU check: `powershell -NoProfile -File .\scripts\verify_training_cpu_pattern.ps1` while training runs.

## Strict training behavior (no fake fallbacks)

- `training` progress counters report only real completed training work (`samples_processed` / `samples_processed_total`), never DS ingestion substitution.
- WUI/API must not send `epochs` overrides; `training.epochs` comes only from the active TOML.
- Invalid config values are hard errors (not silently clamped) in the native init path.
- Missing required training inputs fail explicitly. Synthetic negatives are controlled by `training.allow_generated_negatives` (`true` by default), and can be disabled for strict pair-only operation.

## WUI / MCP training flow

1. **`wui_init_training_pipeline`** — validates TOML paths under `DataDir` only (no native DLL call), so the server stays up if config is wrong.
2. **`wui_start_ff_training`** — loads **`q_training.dll`**, calls `Training_InitSession` + `Training_StartTraining`. If the DLL is missing, wrong architecture, or out of sync with the Go CGO declarations, this step fails with an MCP JSON error instead of killing the whole HTTP connection (“Failed to fetch”).

### Post-prefill / “Poll 1” native crash (first `process_batch`)

If training starts then the process exits with no Go stack, use [q_mini_docs/POST_POLL_NATIVE_DEBUG.md](q_mini_docs/POST_POLL_NATIVE_DEBUG.md): confirm **crash hook** lines and `directory`+`sycl` / **smoke TOML** binary search. For WER dumps, run **`scripts/enable_wer_localdumps.ps1`** elevated.

## Questions

See [GROUND_TRUTH.md](GROUND_TRUTH.md) for capability limits and [README.md](README.md) for project intent.
