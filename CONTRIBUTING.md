# Contributing and cloning

This repo is meant to clone cleanly: **large corpora, weights, and CMake outputs stay out of git** (see `.gitignore`). Use external disk for data when training.

## Prerequisites

- **Go** (1.21+ recommended) with **CGO enabled** on Windows (MSVC toolchain so the host can load `q_training.dll`).
- **CMake** + **Visual Studio** (or another C++20 toolchain supported by the project CMakeLists).

## Clone

```powershell
git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.git
cd q_mini_wasm_v2
```

## Build (minimal path for the WUI + training host)

1. Configure and build the training DLL:

   ```powershell
   cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_final
   cmake --build q_mini_wasm_v2/build_final --config Release --target q_training
   ```

2. Build the Go server **at the repository root** (this is the only user-facing executable):

   ```powershell
   cd C:\GitHub\q_mini_wasm_v2   # or your clone path
   go build -o qminiwasm.exe ./cmd/qminiwasm
   ```

   Or: `powershell -NoProfile -File .\scripts\build_qminiwasm.ps1`

3. **Layout** is defined in [`config/path_map.toml`](config/path_map.toml): canonical **`qminiwasm.exe`** at the repo root, **`wui/`** beside it, and **`q_training.dll`** next to the exe (CGO load order). Building **`q_training`** runs a post-build copy to **both** the repo root and **`native_runtime/q_training.dll`**. If the root DLL is **locked** by a running `qminiwasm.exe`, the alternate copy still updates—stop the server, then copy `native_runtime\q_training.dll` over the root copy (or rebuild after closing the process).

## Data and config (not in git)

The Go host defaults to **`C:\q_mini_data`** for runtime files (`DataDir` in `cmd/qminiwasm/main.go`). Set **`QMINI_DATA_DIR`** to redirect the whole tree (config, datasets, checkpoints). **`QMINI_TRAINING_CONFIG`** can point at any absolute `.toml` (e.g. your repo copy) without moving the rest of the tree. The WUI shows the **active training TOML path** on load; editing only `config/training_config.toml` in git does nothing until that file is copied to the active path or you set the env vars.

Create:

- `C:\q_mini_data\config\training_config.toml` — copy from [config/training_config.toml](config/training_config.toml) in the repo as a starting point.
- `C:\q_mini_data\config\data_sources.toml` — copy from [config/data_sources.toml](config/data_sources.toml).
- `C:\q_mini_data\datasets\` — put `.txt` / `.jsonl` training files here (or set `paths.dataset_dir` in `training_config.toml`).

**Do not commit** downloaded books, weights, or `build_final/` / `build_clean/` trees; they are ignored by design.

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
