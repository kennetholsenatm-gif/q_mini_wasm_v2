"""WASM Execution Engine Module

This module implements the WASM execution engine (Pillar 5) which provides the WebAssembly interpreter
for deterministic in-model execution. It uses the wasmtime Python library to execute WASM code and
capture stack/memory deltas for training.

Key Components:
- WasmExecutor: Main WASM execution engine
- Execution hooks: Memory and stack introspection
- Delta capture: Stack arithmetic and memory mutation tracking
"""

from .engine import WasmExecutor

__all__ = ["WasmExecutor"]