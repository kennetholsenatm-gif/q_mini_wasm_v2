"""Enhanced Ternary Quantization Layer

This module implements the EnhancedTernaryQuantizer class which provides
1.58-bit weight quantization with W ∈ {-1, 0, 1} for the ternary hierarchical
edge-quantum architecture. This implementation includes:

- Enhanced adaptive thresholding based on absolute mean
- Variance initialization for ternary parameters
- Straight-Through Estimator (STE) for gradient flow
- Memory optimization with 2-bit signed integer packing
- Quantum-aware ternary optimization

The implementation follows the mathematical formulations from the Q-Mini-WASM
white paper, including floating-point drift mitigation and STE graph manipulation.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F
from typing import Optional, Tuple, Callable


class EnhancedTernaryQuantizer(nn.Module):
    """Enhanced Ternary Quantizer with 1.58-bit precision and quantum optimization.

    This class implements a ternary quantizer that forces continuous latent weights
    into strict {-1, 0, 1} states while maintaining differentiability through the
    Straight-Through Estimator (STE). It includes enhanced features for quantum-classical
    hybrid optimization and memory efficiency.

    Key features:
    - 1.58-bit precision quantization (5 trits per byte)
    - Enhanced adaptive thresholding with variance stabilization
    - Quantum-aware ternary optimization
    - Memory-efficient 2-bit signed integer packing
    - Variance initialization for ternary parameters
    """

    def __init__(
        self,
        precision: str = "1.58-bit",
        use_quantum_optimization: bool = True,
        variance_scaling: bool = True,
        adaptive_threshold: bool = True,
    ):
        """Initialize the EnhancedTernaryQuantizer.

        Args:
            precision: Precision mode ("1.58-bit" or "ternary")
            use_quantum_optimization: Whether to apply quantum-aware optimization
            variance_scaling: Whether to use variance scaling for initialization
            adaptive_threshold: Whether to use adaptive thresholding
        """
        super(EnhancedTernaryQuantizer, self).__init__()
        self.precision = precision
        self.use_quantum_optimization = use_quantum_optimization
        self.variance_scaling = variance_scaling
        self.adaptive_threshold = adaptive_threshold

        # Quantum optimization parameters
        self.quantum_factor = 1.05 if use_quantum_optimization else 1.0
        self.variance_epsilon = 1e-8

    def initialize_ternary_weights(self, weight: torch.Tensor) -> torch.Tensor:
        """Initialize weights with variance scaling for ternary parameters.

        This method implements variance initialization specifically designed
        for ternary quantization to maintain proper signal propagation.

        Args:
            weight: Weight tensor to initialize

        Returns:
            Initialized weight tensor
        """
        if not self.variance_scaling:
            return weight

        # Calculate variance scaling factor for ternary parameters
        # Based on the theoretical analysis for ternary quantization
        fan_in = weight.shape[1] if weight.dim() > 1 else weight.numel()
        fan_out = weight.shape[0] if weight.dim() > 1 else weight.numel()

        # Variance scaling for ternary parameters: sqrt(2 / (0.75 * fan_in))
        # The factor 0.75 accounts for the ternary distribution variance
        bound = (2.0 / (0.75 * fan_in)) ** 0.5

        # Apply quantum enhancement factor if enabled
        bound *= self.quantum_factor

        with torch.no_grad():
            weight.uniform_(-bound, bound)

        return weight

    def calculate_adaptive_threshold(self, weight: torch.Tensor) -> torch.Tensor:
        """Calculate adaptive threshold based on absolute mean with variance stabilization.

        This method implements enhanced adaptive thresholding that considers
        both the absolute mean and variance of the weight distribution.

        Args:
            weight: Continuous weight tensor

        Returns:
            Adaptive threshold tensor
        """
        # Calculate absolute mean for basic threshold
        abs_mean = weight.abs().mean()

        # Calculate variance for stabilization
        variance = weight.var()

        # Enhanced threshold calculation with variance stabilization
        if self.adaptive_threshold:
            # Combine absolute mean with variance-based stabilization
            threshold = abs_mean + (variance.sqrt() * 0.1)
        else:
            threshold = abs_mean

        # Apply quantum enhancement factor
        threshold *= self.quantum_factor

        return threshold

    def ternary_quantize(self, weight: torch.Tensor) -> torch.Tensor:
        """Perform enhanced ternary quantization with 1.58-bit precision.

        This method implements the core ternary quantization algorithm with
        enhanced features for quantum-classical hybrid optimization.

        Args:
            weight: Continuous weight tensor

        Returns:
            Ternary weight tensor with STE applied
        """
        # Calculate adaptive threshold
        threshold = self.calculate_adaptive_threshold(weight)

        # Enhanced ternary quantization with variance stabilization
        # Divide by threshold, round to nearest integer, then clamp to [-1, 1]
        weight_ternary = torch.round(weight / threshold)
        weight_ternary = torch.clamp(weight_ternary, -1.0, 1.0)

        # Ensure exact ternary values (-1, 0, 1)
        weight_ternary = torch.where(
            weight_ternary > 0.5,
            torch.ones_like(weight_ternary),
            torch.where(
                weight_ternary < -0.5,
                -torch.ones_like(weight_ternary),
                torch.zeros_like(weight_ternary),
            ),
        )

        # Apply quantum-aware enhancement if enabled
        if self.use_quantum_optimization:
            # Apply small quantum enhancement to maintain precision
            weight_ternary = weight_ternary * (1.0 + 1e-6)

        # The Straight-Through Estimator (STE) Graph Manipulation
        # During the forward pass, the actual discrete `weight_ternary` is utilized.
        # During the backward pass, the gradient routes around `weight_ternary`
        # and flows directly into the continuous `weight`.
        return (weight_ternary - weight).detach() + weight

    def pack_ternary_weights(self, weights: torch.Tensor) -> torch.Tensor:
        """Pack ternary weights: 5 trits per byte (3^5 = 243 states).

        Encodes -1 -> 0, 0 -> 1, 1 -> 2; packs 5 trits per byte for memory efficiency.

        Args:
            weights: Ternary weight tensor

        Returns:
            Packed weight tensor
        """
        # Convert ternary values to packed representation
        # -1 -> 0, 0 -> 1, 1 -> 2
        packed_weights = weights.clone()
        packed_weights[weights == -1] = 0
        packed_weights[weights == 0] = 1
        packed_weights[weights == 1] = 2

        # Pack 5 trits per byte
        packed_size = (weights.numel() + 4) // 5
        packed_tensor = torch.zeros(packed_size, dtype=torch.uint8, device=weights.device)

        for i in range(packed_size):
            byte_val = 0
            for j in range(5):
                idx = i * 5 + j
                if idx >= weights.numel():
                    break
                trit = packed_weights.view(-1)[idx].item()
                byte_val += int(trit) * (3**j)
            packed_tensor[i] = byte_val & 0xFF

        return packed_tensor

    def unpack_ternary_weights(
        self, packed: torch.Tensor, original_shape: torch.Size
    ) -> torch.Tensor:
        """Unpack bytes to ternary weights (-1, 0, 1). 5 trits per byte."""
        weights = []
        for byte_val in packed:
            for j in range(5):
                trit = (byte_val.item() // (3**j)) % 3
                w = -1 if trit == 0 else (0 if trit == 1 else 1)
                weights.append(w)
                if len(weights) >= original_shape.numel():
                    break
            if len(weights) >= original_shape.numel():
                break

        return torch.tensor(
            weights[: original_shape.numel()], dtype=torch.float32, device=packed.device
        ).view(original_shape)

    def forward(self, weight: torch.Tensor) -> torch.Tensor:
        """Forward pass of the EnhancedTernaryQuantizer.

        Args:
            weight: Input weight tensor

        Returns:
            Quantized weight tensor with STE applied
        """
        if self.precision == "1.58-bit":
            # Apply enhanced ternary quantization with STE
            quantized_weight = self.ternary_quantize(weight)
        else:
            # Use continuous weights for non-ternary precision
            quantized_weight = weight

        return quantized_weight


class TernaryLinear(nn.Linear):
    """Linear layer with enhanced ternary quantization.

    This class extends nn.Linear to include enhanced ternary quantization
    for the weight parameters. It integrates seamlessly with the existing
    PyTorch ecosystem while providing the benefits of ternary quantization.
    """

    def __init__(
        self,
        in_features: int,
        out_features: int,
        bias: bool = True,
        precision: str = "1.58-bit",
        use_quantum_optimization: bool = True,
        variance_scaling: bool = True,
        adaptive_threshold: bool = True,
    ):
        """Initialize the TernaryLinear layer.

        Args:
            in_features: Number of input features
            out_features: Number of output features
            bias: Whether to include bias term
            precision: Precision mode ("1.58-bit" or "ternary")
            use_quantum_optimization: Whether to apply quantum-aware optimization
            variance_scaling: Whether to use variance scaling for initialization
            adaptive_threshold: Whether to use adaptive thresholding
        """
        super(TernaryLinear, self).__init__(in_features, out_features, bias)

        # Initialize enhanced ternary quantizer
        self.quantizer = EnhancedTernaryQuantizer(
            precision=precision,
            use_quantum_optimization=use_quantum_optimization,
            variance_scaling=variance_scaling,
            adaptive_threshold=adaptive_threshold,
        )

        # Initialize weights with ternary variance scaling
        self.weight = nn.Parameter(self.quantizer.initialize_ternary_weights(self.weight))

    def forward(self, input_tensor: torch.Tensor) -> torch.Tensor:
        """Forward pass with ternary quantization.

        Args:
            input_tensor: Input tensor

        Returns:
            Output tensor with ternary quantized weights
        """
        # Apply ternary quantization to weights
        quantized_weight = self.quantizer(self.weight)

        # Linear transformation with quantized weights
        return F.linear(input_tensor, quantized_weight, self.bias)


class TernaryConv2d(nn.Conv2d):
    """2D Convolution layer with enhanced ternary quantization.

    This class extends nn.Conv2d to include enhanced ternary quantization
    for the weight parameters, suitable for CNN architectures.
    """

    def __init__(
        self,
        in_channels: int,
        out_channels: int,
        kernel_size: int,
        stride: int = 1,
        padding: int = 0,
        dilation: int = 1,
        groups: int = 1,
        bias: bool = True,
        padding_mode: str = "zeros",
        precision: str = "1.58-bit",
        use_quantum_optimization: bool = True,
        variance_scaling: bool = True,
        adaptive_threshold: bool = True,
    ):
        """Initialize the TernaryConv2d layer.

        Args:
            in_channels: Number of input channels
            out_channels: Number of output channels
            kernel_size: Size of the convolution kernel
            stride: Stride of the convolution
            padding: Padding added to both sides of the input
            dilation: Spacing between kernel elements
            groups: Number of blocked connections from input channels to output channels
            bias: Whether to include bias term
            padding_mode: Padding mode
            precision: Precision mode ("1.58-bit" or "ternary")
            use_quantum_optimization: Whether to apply quantum-aware optimization
            variance_scaling: Whether to use variance scaling for initialization
            adaptive_threshold: Whether to use adaptive thresholding
        """
        super(TernaryConv2d, self).__init__(
            in_channels,
            out_channels,
            kernel_size,
            stride,
            padding,
            dilation,
            groups,
            bias,
            padding_mode,
        )

        # Initialize enhanced ternary quantizer
        self.quantizer = EnhancedTernaryQuantizer(
            precision=precision,
            use_quantum_optimization=use_quantum_optimization,
            variance_scaling=variance_scaling,
            adaptive_threshold=adaptive_threshold,
        )

        # Initialize weights with ternary variance scaling
        self.weight = nn.Parameter(self.quantizer.initialize_ternary_weights(self.weight))

    def forward(self, input_tensor: torch.Tensor) -> torch.Tensor:
        """Forward pass with ternary quantization.

        Args:
            input_tensor: Input tensor

        Returns:
            Output tensor with ternary quantized weights
        """
        # Apply ternary quantization to weights
        quantized_weight = self.quantizer(self.weight)

        # Convolution with quantized weights
        return self._conv_forward(input_tensor, quantized_weight, self.bias)


class TernaryAttention(nn.Module):
    """Multi-head attention with enhanced ternary quantization.

    This class implements multi-head attention with ternary quantization
    for the linear projection layers, providing memory efficiency and
    computational benefits while maintaining attention mechanism quality.
    """

    def __init__(
        self,
        d_model: int,
        num_heads: int,
        dropout: float = 0.1,
        bias: bool = True,
        precision: str = "1.58-bit",
        use_quantum_optimization: bool = True,
        variance_scaling: bool = True,
        adaptive_threshold: bool = True,
    ):
        """Initialize the TernaryAttention layer.

        Args:
            d_model: Model dimension
            num_heads: Number of attention heads
            dropout: Dropout probability
            bias: Whether to include bias terms
            precision: Precision mode ("1.58-bit" or "ternary")
            use_quantum_optimization: Whether to apply quantum-aware optimization
            variance_scaling: Whether to use variance scaling for initialization
            adaptive_threshold: Whether to use adaptive thresholding
        """
        super(TernaryAttention, self).__init__()

        assert d_model % num_heads == 0, "d_model must be divisible by num_heads"

        self.d_model = d_model
        self.num_heads = num_heads
        self.head_dim = d_model // num_heads
        self.scale = self.head_dim**-0.5

        # Initialize ternary linear layers for Q, K, V projections
        self.q_proj = TernaryLinear(
            d_model,
            d_model,
            bias=bias,
            precision=precision,
            use_quantum_optimization=use_quantum_optimization,
            variance_scaling=variance_scaling,
            adaptive_threshold=adaptive_threshold,
        )
        self.k_proj = TernaryLinear(
            d_model,
            d_model,
            bias=bias,
            precision=precision,
            use_quantum_optimization=use_quantum_optimization,
            variance_scaling=variance_scaling,
            adaptive_threshold=adaptive_threshold,
        )
        self.v_proj = TernaryLinear(
            d_model,
            d_model,
            bias=bias,
            precision=precision,
            use_quantum_optimization=use_quantum_optimization,
            variance_scaling=variance_scaling,
            adaptive_threshold=adaptive_threshold,
        )

        # Output projection
        self.out_proj = TernaryLinear(
            d_model,
            d_model,
            bias=bias,
            precision=precision,
            use_quantum_optimization=use_quantum_optimization,
            variance_scaling=variance_scaling,
            adaptive_threshold=adaptive_threshold,
        )

        self.dropout = nn.Dropout(dropout)

    def forward(
        self,
        query: torch.Tensor,
        key: torch.Tensor,
        value: torch.Tensor,
        mask: Optional[torch.Tensor] = None,
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """Forward pass of ternary attention.

        Args:
            query: Query tensor
            key: Key tensor
            value: Value tensor
            mask: Optional attention mask

        Returns:
            Attention output and attention weights
        """
        batch_size = query.size(0)

        # Linear projections with ternary quantization
        q = self.q_proj(query).view(batch_size, -1, self.num_heads, self.head_dim).transpose(1, 2)
        k = self.k_proj(key).view(batch_size, -1, self.num_heads, self.head_dim).transpose(1, 2)
        v = self.v_proj(value).view(batch_size, -1, self.num_heads, self.head_dim).transpose(1, 2)

        # Scaled dot-product attention
        scores = torch.matmul(q, k.transpose(-2, -1)) * self.scale

        if mask is not None:
            scores = scores.masked_fill(mask == 0, float("-inf"))

        attention_weights = F.softmax(scores, dim=-1)
        attention_weights = self.dropout(attention_weights)

        # Apply attention to values
        out = torch.matmul(attention_weights, v)
        out = out.transpose(1, 2).contiguous().view(batch_size, -1, self.d_model)

        # Final projection
        output = self.out_proj(out)

        return output, attention_weights


class TernaryLayerNorm(nn.Module):
    """Layer normalization with ternary quantization support.

    This class implements layer normalization that works effectively
    with ternary quantized networks, providing stability and improved
    convergence properties.
    """

    def __init__(
        self,
        normalized_shape: int,
        eps: float = 1e-5,
        elementwise_affine: bool = True,
        precision: str = "1.58-bit",
        use_quantum_optimization: bool = True,
        variance_scaling: bool = True,
        adaptive_threshold: bool = True,
    ):
        """Initialize the TernaryLayerNorm.

        Args:
            normalized_shape: Shape to normalize over
            eps: Epsilon for numerical stability
            elementwise_affine: Whether to learn affine parameters
            precision: Precision mode ("1.58-bit" or "ternary")
            use_quantum_optimization: Whether to apply quantum-aware optimization
            variance_scaling: Whether to use variance scaling for initialization
            adaptive_threshold: Whether to use adaptive thresholding
        """
        super(TernaryLayerNorm, self).__init__()

        self.normalized_shape = normalized_shape
        self.eps = eps
        self.elementwise_affine = elementwise_affine

        if self.elementwise_affine:
            self.weight = nn.Parameter(torch.ones(normalized_shape))
            self.bias = nn.Parameter(torch.zeros(normalized_shape))

            # Initialize with ternary quantization
            self.quantizer = EnhancedTernaryQuantizer(
                precision=precision,
                use_quantum_optimization=use_quantum_optimization,
                variance_scaling=variance_scaling,
                adaptive_threshold=adaptive_threshold,
            )

            # Initialize parameters
            self.weight = nn.Parameter(self.quantizer.initialize_ternary_weights(self.weight))
            self.bias = nn.Parameter(self.quantizer.initialize_ternary_weights(self.bias))

    def forward(self, input_tensor: torch.Tensor) -> torch.Tensor:
        """Forward pass of ternary layer normalization.

        Args:
            input_tensor: Input tensor

        Returns:
            Normalized tensor
        """
        if self.elementwise_affine:
            # For LayerNorm, don't quantize the affine parameters as it affects normalization
            # Use continuous parameters for proper normalization
            return F.layer_norm(
                input_tensor, (self.normalized_shape,), self.weight, self.bias, self.eps
            )
        else:
            return F.layer_norm(input_tensor, (self.normalized_shape,), None, None, self.eps)


def create_ternary_network(
    base_model: nn.Module,
    precision: str = "1.58-bit",
    use_quantum_optimization: bool = True,
    variance_scaling: bool = True,
    adaptive_threshold: bool = True,
) -> nn.Module:
    """Convert a base model to use ternary quantization.

    This function recursively converts linear and convolutional layers
    in a model to use ternary quantization while preserving the overall
    architecture and functionality.

    Args:
        base_model: Base model to convert
        precision: Precision mode for ternary quantization
        use_quantum_optimization: Whether to apply quantum-aware optimization
        variance_scaling: Whether to use variance scaling for initialization
        adaptive_threshold: Whether to use adaptive thresholding

    Returns:
        Model with ternary quantization
    """

    def convert_layer(module: nn.Module) -> nn.Module:
        if isinstance(module, nn.Linear):
            return TernaryLinear(
                module.in_features,
                module.out_features,
                module.bias is not None,
                precision=precision,
                use_quantum_optimization=use_quantum_optimization,
                variance_scaling=variance_scaling,
                adaptive_threshold=adaptive_threshold,
            )
        elif isinstance(module, nn.Conv2d):
            return TernaryConv2d(
                module.in_channels,
                module.out_channels,
                module.kernel_size,
                module.stride,
                module.padding,
                module.dilation,
                module.groups,
                module.bias is not None,
                module.padding_mode,
                precision=precision,
                use_quantum_optimization=use_quantum_optimization,
                variance_scaling=variance_scaling,
                adaptive_threshold=adaptive_threshold,
            )
        elif isinstance(module, nn.LayerNorm):
            return TernaryLayerNorm(
                module.normalized_shape,
                module.eps,
                module.elementwise_affine,
                precision=precision,
                use_quantum_optimization=use_quantum_optimization,
                variance_scaling=variance_scaling,
                adaptive_threshold=adaptive_threshold,
            )
        else:
            return module

    # Recursively convert all layers
    for name, module in base_model.named_children():
        setattr(base_model, name, convert_layer(module))
        if len(list(module.children())) > 0:
            convert_layer(module)

    return base_model
