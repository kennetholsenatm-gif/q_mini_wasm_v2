"""Data Module

This module implements the Synthetic Data Pipeline (Pillar 5) which provides the data infrastructure
for training the Q-Mini-WASM architecture. It includes Wasmtime instrumentation, fault injection,
and state recovery mechanisms.

Key Components:
- DataPipeline: Main data pipeline interface
- WasmtimeInstrumenter: Wasmtime instrumentation for training
- FaultInjector: Fault injection for robustness training
- StateRecovery: State recovery mechanisms
"""

from .pipeline import DataPipeline

__all__ = ["DataPipeline"]