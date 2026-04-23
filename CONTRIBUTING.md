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

2. Build the Go server:

   ```powershell
   go build -o cmd/qminiwasm/qminiwasm.exe ./cmd/qminiwasm
   ```

3. Place **`q_training.dll`** (from `q_mini_wasm_v2/build_final/`) next to **`cmd/qminiwasm/qminiwasm.exe`**, or ensure the directory containing the DLL is on `PATH` so the dynamic loader can resolve it.

## Data and config (not in git)

The Go host defaults to **`C:\q_mini_data`** for runtime files (`DataDir` in `cmd/qminiwasm/main.go`). Create:

- `C:\q_mini_data\config\training_config.toml` — copy from [config/training_config.toml](config/training_config.toml) in the repo as a starting point.
- `C:\q_mini_data\config\data_sources.toml` — copy from [config/data_sources.toml](config/data_sources.toml).
- `C:\q_mini_data\datasets\` — put `.txt` / `.jsonl` training files here (or set `paths.dataset_dir` in `training_config.toml`).

**Do not commit** downloaded books, weights, or `build_final/` / `build_clean/` trees; they are ignored by design.

## What to commit

- Source under `q_mini_wasm_v2/`, `cmd/qminiwasm/`, `config/` (templates), `wui/`, docs you want versioned (e.g. `q_mini_docs/`).
- Avoid committing `*.exe`, `*.dll`, logs, CSV audit dumps, and extracted release assets.

## Questions

See [GROUND_TRUTH.md](GROUND_TRUTH.md) for capability limits and [README.md](README.md) for project intent.
