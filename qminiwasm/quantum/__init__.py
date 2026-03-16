"""Quantum MoE Router Module

This module implements the Quantum MoE Router (Pillar 1) which reformulates MoE routing
as a discrete combinatorial optimization problem mapped to a quantum topology. It uses
Parameterized Quantum Circuits (PQCs) and the Quantum Approximate Optimization
Algorithm (QAOA) to solve the routing problem with perfect load balancing.

Key Components:
- HybridQuantumMoE: Main quantum MoE router class
- quantum_router_circuit: PennyLane quantum circuit implementation
- get_backend: Quantum backend factory (PennyLane / IBM / Intel QS) for the interconnect
- Intel Quantum: Optional Intel Quantum SDK / Intel QS integration (intel_backend)
"""

from .router import QuantumRouter as HybridQuantumMoE
from .backend_registry import get_backend
from . import intel_backend

__all__ = ["HybridQuantumMoE", "get_backend", "intel_backend"]
