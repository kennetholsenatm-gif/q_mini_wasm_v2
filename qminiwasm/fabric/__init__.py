"""Quantum-Assisted Hierarchical Routing (QAHR)

Tier-3 escalation routing as combinatorial optimization (QUBO / QAOA), not informal
MoE load balancing. Key Components:

- QAHRRouter: main QAHR torch block (alias ``HybridQuantumMoE`` for compatibility)
- quantum_router_circuit: PennyLane quantum circuit implementation
- get_backend: Quantum backend factory (PennyLane / IBM / Intel QS) for the interconnect
- Intel Quantum: Optional Intel Quantum SDK / Intel QS integration (intel_backend)
"""

from .router import HybridQuantumMoE, QAHRRouter
from .backend_registry import get_backend
from . import intel_backend

__all__ = ["QAHRRouter", "HybridQuantumMoE", "get_backend", "intel_backend"]
