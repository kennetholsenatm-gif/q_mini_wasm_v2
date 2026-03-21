"""Q-Mini-WASM: ternary-quantized layers, PennyLane (local sim) routing, and Wasmtime execution.

Main export: ``QMiniWASM`` — top-level model interface.
"""

from .model import QMiniWASM

__version__ = "0.1.0"
__all__ = ["QMiniWASM"]
