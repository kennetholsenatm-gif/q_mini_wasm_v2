"""Legacy package alias: re-exports :mod:`qminiwasm.wasm_host` at the top level only.

For submodule imports (e.g. ``trit_pack``), use :mod:`qminiwasm.wasm_host` directly.
"""

from qminiwasm.wasm_host import *  # noqa: F403

__all__ = [  # keep in sync with qminiwasm.wasm_host
    "WasmRuntimeConfig",
    "WasmEngine",
    "WasmCompiler",
    "MESH_ALGORITHMS",
    "MESH_EXPORT_NAMES",
    "wasm_module_needs_wasi",
    "instantiate_wasmtime_module",
    "build_clang_wasm_compile_command",
    "trit_pack",
    "tpem_bundle",
    "compress_deltas",
    "delta_payload_struct",
]
