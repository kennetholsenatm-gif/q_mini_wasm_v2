"""Quantum MoE Router Module

This module implements the Quantum MoE Router (Pillar 1) which reformulates MoE routing as a discrete combinatorial
optimization problem mapped to a quantum topology. It uses Parameterized Quantum Circuits (PQCs) and the Quantum
Approximate Optimization Algorithm (QAOA) to solve the routing problem with perfect load balancing.

Key Components:
- HybridQuantumMoE: Main quantum MoE router class
- quantum_router_circuit: PennyLane quantum circuit implementation
- QAOA parameterizations: Quantum circuit parameters for optimization
- Dense Angle Embedding: Dimensionality reduction for NISQ encoding
"""

from .router import HybridQuantumMoE

__all__ = ["HybridQuantumMoE"]