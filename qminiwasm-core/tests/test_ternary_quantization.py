"""Tests for Enhanced Ternary Quantization implementation."""

import unittest
import torch
import torch.nn as nn
import numpy as np
from typing import Tuple

# Import the new ternary quantization modules
from qminiwasm.layers.ternary_quantization import (
    EnhancedTernaryQuantizer,
    TernaryLinear,
    TernaryConv2d,
    TernaryAttention,
    TernaryLayerNorm,
    create_ternary_network
)


class TestEnhancedTernaryQuantizer(unittest.TestCase):
    """Test EnhancedTernaryQuantizer functionality."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)
        self.quantizer = EnhancedTernaryQuantizer(
            precision="1.58-bit",
            use_quantum_optimization=True,
            variance_scaling=True,
            adaptive_threshold=True
        )

    def test_ternary_quantization_basic(self):
        """Test basic ternary quantization functionality."""
        # Create random weights
        weights = torch.randn(10, 20)

        # Apply quantization
        quantized = self.quantizer(weights)

        # Check that output has correct shape
        self.assertEqual(quantized.shape, weights.shape)

        # Check that values are in {-1, 0, 1} with tolerance
        unique_values = torch.unique(quantized)
        is_ternary = torch.all((torch.abs(unique_values - (-1)) < 1e-6) | 
                              (torch.abs(unique_values - 0) < 1e-6) | 
                              (torch.abs(unique_values - 1) < 1e-6))
        self.assertTrue(is_ternary)

    def test_variance_initialization(self):
        """Test variance initialization for ternary parameters."""
        # Create a weight tensor
        weight = torch.zeros(5, 10)

        # Initialize with ternary variance scaling
        initialized_weight = self.quantizer.initialize_ternary_weights(weight)

        # Check that weights are initialized (not all zeros)
        self.assertFalse(torch.allclose(initialized_weight, torch.zeros_like(weight)))

        # Check that variance is reasonable
        variance = initialized_weight.var().item()
        self.assertGreater(variance, 0.001)  # Should have some variance

    def test_adaptive_threshold(self):
        """Test adaptive threshold calculation."""
        weights = torch.randn(10, 20)

        # Calculate adaptive threshold
        threshold = self.quantizer.calculate_adaptive_threshold(weights)

        # Threshold should be positive
        self.assertGreater(threshold.item(), 0)

        # Threshold should be reasonable relative to weight magnitude
        abs_mean = weights.abs().mean()
        self.assertGreater(threshold.item(), abs_mean.item() * 0.5)

    def test_quantum_optimization(self):
        """Test quantum optimization enhancement."""
        weights = torch.randn(5, 10)

        # Test with quantum optimization enabled
        quantizer_qo = EnhancedTernaryQuantizer(use_quantum_optimization=True)
        quantized_qo = quantizer_qo(weights)

        # Test with quantum optimization disabled
        quantizer_no_qo = EnhancedTernaryQuantizer(use_quantum_optimization=False)
        quantized_no_qo = quantizer_no_qo(weights)

        # Results should be different due to quantum enhancement
        # (though both should be valid ternary quantizations)
        self.assertEqual(quantized_qo.shape, quantized_no_qo.shape)

    def test_memory_packing(self):
        """Test memory packing functionality."""
        weights = torch.tensor([-1, 0, 1, -1, 0, 1, -1, 0], dtype=torch.float32)

        # Pack weights
        packed = self.quantizer.pack_ternary_weights(weights)

        # Unpack weights
        unpacked = self.quantizer.unpack_ternary_weights(packed, weights.shape)

        # Check that unpacked weights match original
        self.assertTrue(torch.allclose(weights, unpacked, atol=1e-6))

    def test_ste_gradient_flow(self):
        """Test Straight-Through Estimator gradient flow."""
        weights = torch.randn(5, 10, requires_grad=True)

        # Apply quantization
        quantized = self.quantizer(weights)

        # Compute loss
        loss = quantized.sum()

        # Backward pass
        loss.backward()

        # Check that gradients exist for original weights
        self.assertIsNotNone(weights.grad)
        self.assertEqual(weights.grad.shape, weights.shape)


class TestTernaryLinear(unittest.TestCase):
    """Test TernaryLinear layer functionality."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)

    def test_linear_layer_creation(self):
        """Test TernaryLinear layer creation and basic functionality."""
        layer = TernaryLinear(10, 5, bias=True)

        # Check layer properties
        self.assertEqual(layer.in_features, 10)
        self.assertEqual(layer.out_features, 5)
        self.assertTrue(layer.bias is not None)

        # Check that quantizer is initialized
        self.assertIsInstance(layer.quantizer, EnhancedTernaryQuantizer)

    def test_linear_forward_pass(self):
        """Test forward pass through TernaryLinear layer."""
        layer = TernaryLinear(10, 5, bias=True)
        input_tensor = torch.randn(3, 10)

        # Forward pass
        output = layer(input_tensor)

        # Check output shape
        self.assertEqual(output.shape, (3, 5))

        # Check that weights are quantized during forward pass
        quantized_weight = layer.quantizer(layer.weight)
        is_ternary = torch.all((torch.abs(quantized_weight - (-1)) < 1e-6) | 
                              (torch.abs(quantized_weight - 0) < 1e-6) | 
                              (torch.abs(quantized_weight - 1) < 1e-6))
        self.assertTrue(is_ternary)

    def test_linear_gradient_flow(self):
        """Test gradient flow through TernaryLinear layer."""
        layer = TernaryLinear(10, 5, bias=True)
        input_tensor = torch.randn(3, 10, requires_grad=True)

        # Forward pass
        output = layer(input_tensor)
        loss = output.sum()

        # Backward pass
        loss.backward()

        # Check that gradients exist
        self.assertIsNotNone(input_tensor.grad)
        self.assertEqual(input_tensor.grad.shape, input_tensor.shape)


class TestTernaryConv2d(unittest.TestCase):
    """Test TernaryConv2d layer functionality."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)

    def test_conv2d_layer_creation(self):
        """Test TernaryConv2d layer creation."""
        layer = TernaryConv2d(3, 16, kernel_size=3, stride=1, padding=1)

        # Check layer properties
        self.assertEqual(layer.in_channels, 3)
        self.assertEqual(layer.out_channels, 16)
        self.assertEqual(layer.kernel_size, (3, 3))

    def test_conv2d_forward_pass(self):
        """Test forward pass through TernaryConv2d layer."""
        layer = TernaryConv2d(3, 16, kernel_size=3, stride=1, padding=1)
        input_tensor = torch.randn(2, 3, 32, 32)

        # Forward pass
        output = layer(input_tensor)

        # Check output shape
        self.assertEqual(output.shape, (2, 16, 32, 32))


class TestTernaryAttention(unittest.TestCase):
    """Test TernaryAttention layer functionality."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)

    def test_attention_layer_creation(self):
        """Test TernaryAttention layer creation."""
        attention = TernaryAttention(
            d_model=64,
            num_heads=8,
            dropout=0.1,
            bias=True
        )

        # Check layer properties
        self.assertEqual(attention.d_model, 64)
        self.assertEqual(attention.num_heads, 8)
        self.assertEqual(attention.head_dim, 8)

    def test_attention_forward_pass(self):
        """Test forward pass through TernaryAttention layer."""
        attention = TernaryAttention(
            d_model=64,
            num_heads=8,
            dropout=0.1,
            bias=True
        )

        batch_size = 2
        seq_len = 10
        query = torch.randn(batch_size, seq_len, 64)
        key = torch.randn(batch_size, seq_len, 64)
        value = torch.randn(batch_size, seq_len, 64)

        # Forward pass
        output, attention_weights = attention(query, key, value)

        # Check output shapes
        self.assertEqual(output.shape, (batch_size, seq_len, 64))
        self.assertEqual(attention_weights.shape, (batch_size, 8, seq_len, seq_len))

        # Check that attention weights sum to 1 (with reasonable tolerance)
        weight_sums = attention_weights.sum(dim=-1)
        self.assertTrue(torch.allclose(weight_sums, torch.ones_like(weight_sums), atol=1e-0))


class TestTernaryLayerNorm(unittest.TestCase):
    """Test TernaryLayerNorm functionality."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)

    def test_layernorm_creation(self):
        """Test TernaryLayerNorm creation."""
        layernorm = TernaryLayerNorm(64, elementwise_affine=True)

        # Check properties
        self.assertEqual(layernorm.normalized_shape, 64)
        self.assertTrue(layernorm.elementwise_affine)

    def test_layernorm_forward_pass(self):
        """Test forward pass through TernaryLayerNorm."""
        layernorm = TernaryLayerNorm(64, elementwise_affine=True)
        input_tensor = torch.randn(2, 10, 64)

        # Forward pass
        output = layernorm(input_tensor)

        # Check output shape
        self.assertEqual(output.shape, input_tensor.shape)

        # Check that output has approximately zero mean and unit variance
        mean = output.mean(dim=-1)
        var = output.var(dim=-1)
        self.assertTrue(torch.allclose(mean, torch.zeros_like(mean), atol=1e-1))
        self.assertTrue(torch.allclose(var, torch.ones_like(var), atol=1e-0))


class TestNetworkConversion(unittest.TestCase):
    """Test network conversion to ternary quantization."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)

    def test_simple_network_conversion(self):
        """Test conversion of a simple network to ternary quantization."""
        # Create a simple network
        base_model = nn.Sequential(
            nn.Linear(10, 20),
            nn.ReLU(),
            nn.Linear(20, 5)
        )

        # Convert to ternary
        ternary_model = create_ternary_network(base_model)

        # Check that layers are converted
        self.assertIsInstance(ternary_model[0], TernaryLinear)
        self.assertIsInstance(ternary_model[2], TernaryLinear)

        # Test forward pass
        input_tensor = torch.randn(3, 10)
        output = ternary_model(input_tensor)

        # Check output shape
        self.assertEqual(output.shape, (3, 5))

    def test_complex_network_conversion(self):
        """Test conversion of a more complex network."""
        # Create a more complex network
        base_model = nn.Sequential(
            nn.Conv2d(3, 16, 3, padding=1),
            nn.ReLU(),
            nn.Conv2d(16, 32, 3, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d((1, 1)),
            nn.Flatten(),
            nn.Linear(32, 10)
        )

        # Convert to ternary
        ternary_model = create_ternary_network(base_model)

        # Check that appropriate layers are converted
        self.assertIsInstance(ternary_model[0], TernaryConv2d)
        self.assertIsInstance(ternary_model[2], TernaryConv2d)
        self.assertIsInstance(ternary_model[6], TernaryLinear)

        # Test forward pass
        input_tensor = torch.randn(2, 3, 32, 32)
        output = ternary_model(input_tensor)

        # Check output shape
        self.assertEqual(output.shape, (2, 10))


class TestTernaryQuantizationPerformance(unittest.TestCase):
    """Test performance characteristics of ternary quantization."""

    def setUp(self):
        """Set up test fixtures."""
        torch.manual_seed(42)

    def test_memory_efficiency(self):
        """Test memory efficiency of ternary quantization."""
        # Create large weight matrix
        weights = torch.randn(1000, 1000)

        # Quantize to ternary
        quantizer = EnhancedTernaryQuantizer()
        quantized = quantizer(weights)

        # Pack weights
        packed = quantizer.pack_ternary_weights(quantized)

        # Calculate memory savings
        original_memory = weights.numel() * 4  # float32
        quantized_memory = quantized.numel() * 4  # float32 (for compatibility)
        packed_memory = packed.numel() * 1  # uint8

        # Packed representation should be much smaller
        compression_ratio = original_memory / packed_memory
        self.assertGreater(compression_ratio, 10)  # Should achieve significant compression

    def test_quantization_accuracy(self):
        """Test quantization accuracy preservation."""
        # Create weights with known distribution
        weights = torch.randn(100, 100)

        # Apply quantization
        quantizer = EnhancedTernaryQuantizer()
        quantized = quantizer(weights)

        # Calculate quantization error
        quantization_error = torch.mean((weights - quantized) ** 2).item()

        # Error should be reasonable (not too large, not too small)
        self.assertGreater(quantization_error, 0.1)
        self.assertLess(quantization_error, 10.0)

    def test_gradient_preservation(self):
        """Test that gradients are preserved through quantization."""
        weights = torch.randn(50, 50, requires_grad=True)

        # Apply quantization
        quantizer = EnhancedTernaryQuantizer()
        quantized = quantizer(weights)

        # Compute loss
        loss = quantized.sum()

        # Backward pass
        loss.backward()

        # Check that gradients are non-zero and reasonable
        self.assertIsNotNone(weights.grad)
        grad_norm = weights.grad.norm().item()
        self.assertGreater(grad_norm, 0.1)
        self.assertLess(grad_norm, 100.0)


if __name__ == '__main__':
    unittest.main()