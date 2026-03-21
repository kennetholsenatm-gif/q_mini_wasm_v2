"""Tests Module

This module contains test cases for the Q-Mini-WASM architecture. It includes:
- Unit tests for individual components
- Integration tests for model functionality
- Performance tests for quantum routing and WASM execution

Key Components:
- TestQMiniWASM: Main test class for QMiniWASM
- TestQuantumRouter: Tests for quantum MoE router
- TestTernaryQuantization: Tests for ternary quantization
- TestWasmEngine: Tests for WASM execution engine
"""

try:
    from .test_qminiwasm import TestQMiniWASM  # noqa: F401

    __all__ = ["TestQMiniWASM"]
except ImportError:
    __all__ = []
