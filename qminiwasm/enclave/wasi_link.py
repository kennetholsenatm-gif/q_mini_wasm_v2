"""WASI-aware wasmtime instantiation (linker + WasiConfig).

Bare wasm32 modules (no WASI imports) use ``Instance(store, module, [])``.
Modules linked against ``wasi_snapshot_preview1`` (and related module names)
must be instantiated through a ``Linker`` with ``define_wasi()`` and a
``WasiConfig`` on the store.

Optional C compilation: set ``QMINIWASM_WASM_C_LINK=wasip1`` and ``WASI_SDK_PATH``
to use the wasi-sdk ``clang`` (wasm32-wasip1) instead of bare ``wasm32``.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import List

import wasmtime

# Import module names seen from clang/wasi-sdk and common toolchains.
_WASI_IMPORT_MODULES = frozenset(
    (
        "wasi_snapshot_preview1",
        "wasi_snapshot_preview2",
        "wasi_unstable",
        "wasi_preview0",
    )
)


def wasm_module_needs_wasi(module: wasmtime.Module) -> bool:
    """True if the module imports any WASI namespace (preview1/preview2/legacy)."""
    for imp in module.imports:
        m = imp.module
        if m in _WASI_IMPORT_MODULES or m.startswith("wasi:"):
            return True
    return False


def instantiate_wasmtime_module(
    store: wasmtime.Store, module: wasmtime.Module
) -> wasmtime.Instance:
    """Instantiate ``module`` with WASI definitions when imports require it."""
    if not wasm_module_needs_wasi(module):
        return wasmtime.Instance(store, module, [])

    linker = wasmtime.Linker(store.engine)
    linker.define_wasi()
    config = wasmtime.WasiConfig()
    store.set_wasi(config)
    return linker.instantiate(store, module)


def wasi_sdk_clang_executable(sdk_root: str) -> str:
    """Path to wasi-sdk ``clang`` (``clang.exe`` on Windows)."""
    name = "clang.exe" if sys.platform == "win32" else "clang"
    return str(Path(sdk_root).expanduser().resolve() / "bin" / name)


def default_wasm_c_link_mode() -> str:
    """Read ``QMINIWASM_WASM_C_LINK`` (default ``bare``)."""
    return os.environ.get("QMINIWASM_WASM_C_LINK", "bare").strip().lower() or "bare"


def default_wasi_sdk_path() -> str | None:
    """Read ``WASI_SDK_PATH`` if set and non-empty."""
    p = os.environ.get("WASI_SDK_PATH", "").strip()
    return p or None


def build_clang_wasm_compile_command(
    c_file: str,
    out_wasm: str,
    *,
    link: str | None = None,
    wasi_sdk_path: str | None = None,
) -> List[str]:
    """Assemble a ``clang`` argv for mesh-style freestanding C (``--no-entry``, ``--export-all``).

    * ``bare`` — system ``clang``, ``-target wasm32``, no WASI.
    * ``wasip1`` — wasi-sdk ``clang``, ``--target=wasm32-wasip1`` (requires ``wasi_sdk_path``).
    """
    mode = (link or default_wasm_c_link_mode()).strip().lower()
    if mode in ("bare", "wasm32"):
        return [
            "clang",
            "-target",
            "wasm32",
            "-nostdlib",
            "-Wl,--no-entry",
            "-Wl,--export-all",
            "-o",
            out_wasm,
            c_file,
        ]
    if mode in ("wasip1", "wasi", "wasi-p1", "wasm32-wasip1"):
        sdk = wasi_sdk_path if wasi_sdk_path is not None else default_wasi_sdk_path()
        if not sdk:
            raise ValueError(
                "WASI_SDK_PATH must be set when QMINIWASM_WASM_C_LINK is wasip1 "
                "(install wasi-sdk and export WASI_SDK_PATH)"
            )
        cc = wasi_sdk_clang_executable(sdk)
        return [
            cc,
            "--target=wasm32-wasip1",
            "-nostdlib",
            "-Wl,--no-entry",
            "-Wl,--export-all",
            "-o",
            out_wasm,
            c_file,
        ]
    raise ValueError(f"Unknown QMINIWASM_WASM_C_LINK={mode!r}; use bare or wasip1")
