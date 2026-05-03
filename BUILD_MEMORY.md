# Build Memory (Do This Every Time)

If builds start failing, use this exact path first.

## Canonical command

From repo root:

```powershell
cd C:\GitHub\q_mini_wasm_v2
powershell -NoProfile -File .\scripts\build_qminiwasm.ps1
```

This script is the source of truth. It:

- loads Intel oneAPI (`setvars.bat`)
- loads Visual Studio toolchain (`vcvars64.bat`)
- configures SYCL build with Intel compiler + Ninja
- builds `q_training.dll`
- copies `q_training.dll` to repo root and `native_runtime\`
- builds `qminiwasm.exe`
- copies required Intel runtime DLLs next to `qminiwasm.exe`

## Why direct `cmake --build` often fails

`cmake --build` can force CMake reconfigure. If your shell does not have oneAPI + VS environment loaded, configure fails with "no SYCL-capable compiler" or MSVC-only toolchain errors.

## Non-negotiables

- Do not use old non-SYCL build directories.
- Use `q_mini_wasm_v2/build_sycl` for native SYCL builds.
- Do not disable SYCL for native training (`q_training` requires SYCL).

## Quick success check

After build, these must exist:

- `C:\GitHub\q_mini_wasm_v2\qminiwasm.exe`
- `C:\GitHub\q_mini_wasm_v2\q_training.dll`
- `C:\GitHub\q_mini_wasm_v2\native_runtime\q_training.dll`

