"""WASM Module

This module provides the core WASM compilation and execution engine for the Q-Mini-WASM architecture.
It handles:
- C source compilation to WASM modules
- WASM module instantiation and execution
- Stack and memory state capture
- Error handling and validation
"""

from .engine import WasmEngine, WasmCompiler, MESH_ALGORITHMS

__all__ = ["WasmEngine", "WasmCompiler", "MESH_ALGORITHMS"]