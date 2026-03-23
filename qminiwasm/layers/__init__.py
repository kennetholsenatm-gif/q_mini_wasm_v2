"""Layers Module

This module implements the core neural network layers for the Q-Mini-WASM architecture, including:
- Ternary quantization for WASM execution experts
- Tropical Attention for geometric hull queries
- Various specialized layers for hybrid quantum-classical execution

Key Components:
- TernaryWASMExpert: Ternary quantization expert with Straight-Through Estimator
- TropicalAttention: Geometric attention mechanism with max-plus algebra
- Various specialized layers for hybrid execution
"""

from .attention import TropicalAttention
from .lota import LoRALinearSide, merge_lora_into_linear_weight
from .ptqtp import PTQTPLinear, decompose_ptqtp, ptqtp_linear_forward
from .ternary import TernaryWASMExpert

__all__ = [
    "TernaryWASMExpert",
    "TropicalAttention",
    "LoRALinearSide",
    "merge_lora_into_linear_weight",
    "PTQTPLinear",
    "decompose_ptqtp",
    "ptqtp_linear_forward",
]
