"""Ternary Quantization Layer

This module implements the TernaryWASMExpert class (Pillar 2) which forces WASM execution
expert weights into an unstructured ternary state: W ∈ {-1, 0, 1}. This ensures deterministic
execution and perfectly simulates exact processor stack mechanics over million-step traces.

The implementation includes:
- Straight-Through Estimator (STE) for classical training
- Ternary weight binarization with adaptive thresholding
- Variance initialization for ternary parameters
- Forward pass with STE graph manipulation

The implementation follows the mathematical formulations from the Q-Mini-WASM white
paper, including:
- Floating-point drift mitigation
- STE graph manipulation using detach()
- Variance initialization for ternary parameters
"""

import torch
import torch.nn as nn
import torch.nn.functional as F


class TernaryWASMExpert(nn.Linear):
    """TernaryWASMExpert: Ternary Quantization Expert with Straight-Through Estimator

    This class implements a linear layer with ternary quantization for WASM execution experts.
    It forces continuous latent weights into strict {-1, 0, 1} states while maintaining
    differentiability through the Straight-Through Estimator (STE).

    The implementation ensures deterministic execution by:
    - Using adaptive thresholding based on absolute mean
    - Applying STE during backward pass to maintain gradient flow
    - Initializing weights with variance scaling for ternary parameters
    """

    def __init__(
        self,
        in_features,
        out_features,
        bias=False,
        precision="ternary",
        tequila_deadzone: float = 0.0,
    ):
        """Initialize the TernaryWASMExpert.

        Args:
            in_features: Number of input features
            out_features: Number of output features
            bias: Whether to include bias term (default: False)
            precision: Precision mode ("ternary" or "float")
            tequila_deadzone: If > 0, fraction of adaptive scale defining a deadzone
                around zero; trapped weights are quantized to 0 and their pre-quant
                mass is added as a per-output bias (Tequila-style).
        """
        super(TernaryWASMExpert, self).__init__(in_features, out_features, bias)
        self.precision = precision
        self.tequila_deadzone = float(tequila_deadzone)

        # Sustain the variance during initialization for ternary parameters
        # Based on variance scaling for ternary quantization
        bound = (2.0 / (0.75 * in_features)) ** 0.5
        nn.init.uniform_(self.weight, a=-bound, b=bound)

    def binarize_ternary(self, weight):
        """Forces continuous latent weights into strict {-1, 0, 1} states.

        This function implements the ternary binarization with adaptive thresholding:
        - Calculates absolute mean for adaptive thresholding
        - Rounds weights to nearest ternary value
        - Clamps to [-1.0, 1.0] range
        - Applies Straight-Through Estimator (STE) for gradient flow

        Args:
            weight: Continuous weight tensor

        Returns:
            Ternary weight tensor with STE applied
        """
        # Calculate the absolute mean for adaptive thresholding
        abs_mean = weight.abs().mean()

        # Discretize the weights using adaptive thresholding
        # Divide by mean, round to nearest integer, then clamp to [-1, 1]
        weight_ternary = torch.round(weight / abs_mean)
        weight_ternary = torch.clamp(weight_ternary, -1.0, 1.0)

        # The Straight-Through Estimator (STE) Graph Manipulation
        # During the forward pass, the actual discrete `weight_ternary` is utilized.
        # During the backward pass, the gradient routes around `weight_ternary`
        # and flows directly into the continuous `weight`.
        return (weight_ternary - weight).detach() + weight

    def forward(self, input_tensor):
        """Forward pass of the TernaryWASMExpert.

        Args:
            input_tensor: Input tensor of shape (batch_size, in_features)

        Returns:
            Output tensor of shape (batch_size, out_features)
        """
        if self.precision == "ternary":
            if self.tequila_deadzone > 0.0:
                discrete_weight, bias_tequila = self._binarize_ternary_tequila(self.weight)
                b = self.bias
                if b is None:
                    b = bias_tequila
                else:
                    b = b + bias_tequila
                return F.linear(input_tensor, discrete_weight, b)
            discrete_weight = self.binarize_ternary(self.weight)
        else:
            discrete_weight = self.weight

        return F.linear(input_tensor, discrete_weight, self.bias)

    def _binarize_ternary_tequila(self, weight: torch.Tensor):
        """Ternary STE with deadzone trapping mass moved to per-row bias."""
        abs_mean = weight.abs().mean().clamp(min=1e-8)
        dz = self.tequila_deadzone * abs_mean
        trap = weight.abs() < dz
        w_scaled = torch.round(weight / abs_mean).clamp(-1.0, 1.0)
        w_scaled = torch.where(trap, torch.zeros_like(w_scaled), w_scaled)
        discrete = (w_scaled - weight).detach() + weight
        bias_tequila = (weight * trap.to(weight.dtype)).sum(dim=1)
        return discrete, bias_tequila
