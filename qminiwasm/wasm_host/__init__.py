"""WASM host runtime (canonical).

This package provides the core WASM compilation and execution engine for the
Q-Mini-WASM architecture. It handles:
- C source compilation to WASM modules
- WASM module instantiation and execution
- Stack and memory state capture
- Error handling and validation
"""

from .engine import MESH_EXPORT_NAMES, WasmEngine, WasmCompiler, MESH_ALGORITHMS, WasmRuntimeConfig
from .wasi_link import (
    build_clang_wasm_compile_command,
    instantiate_wasmtime_module,
    wasm_module_needs_wasi,
)
from . import trit_pack
from . import tpem_bundle
from .delta_compression import compress_deltas, delta_payload_struct

__all__ = [
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
