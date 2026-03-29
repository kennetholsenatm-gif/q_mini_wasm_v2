# Configuration policy

## Revision-controlled config first

Operational settings (runtime mode, gRPC address, RunPod SSH defaults, implementation toggles) belong in TOML under the repository, for example:

- `configs/wui.toml` — Training WUI (`training-wui`): training engine mode, gRPC endpoint, `[wui.runpod]`, `[wui.runtime_profile]`.
- `configs/runtime.toml` — Python `qminiwasm` runtime toggles (loaded by `qminiwasm.runtime_modes` where implemented).

CLI flags on the WUI (e.g. `-training-runtime`, `-grpc-addr`, `-wui-config`) may override file values for a single process.

## Environment variables: secrets and host injection only

Use **environment variables** for:

- **Secrets**: API tokens, keys, passwords (e.g. `.env` for IBM Quantum, Hugging Face, RunPod queue keys).
- **Ephemeral host context**: paths and IDs that are not portable (CI job IDs, machine-specific paths) when no file-based override exists.

Do **not** use environment variables as the primary way to set non-secret product configuration that should be shared with the team or reproduced in CI. Prefer TOML in `configs/` plus documented CLI overrides.

## Legacy `QMINIWASM_*` training vars

The WUI still injects a small set of `QMINIWASM_*` variables into Python subprocesses so existing `qminiwasm.engine` code paths see the same values as `configs/wui.toml`. Prefer aligning Python with `configs/runtime.toml` over growing new env-based knobs.

## Convenience scripts

- **`scripts/start-training-stack.sh`** / **`scripts/start-training-stack.ps1`** — configure/build the C++ **`qminiwasm_training_engine_server`** via CMake when the binary is missing (needs **`cmake`** on `PATH` and gRPC/toolchain deps; see **`cpp/training/README.md`**), start it on **`127.0.0.1:50061`**, then run **`training-wui`** with `-root` set to the repository root. If the build fails, the WUI still starts; with default **`native`** training mode, use **`training_runtime_mode = "auto"`** or **`python`** in **`configs/wui.toml`** until the C++ server is available.
