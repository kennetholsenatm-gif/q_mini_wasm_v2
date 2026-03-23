# Ternary Quantization Integration

This document describes how ternary quantization (BitNet-style) is integrated into the Q-Mini-WASM framework.

## Overview

The framework implements ternary weight quantization where neural network weights are constrained to values `{-1, 0, 1}`. This is achieved through the `TernaryWASMExpert` layer, which applies quantization during the forward pass while maintaining differentiability during training.

## Implementation

### TernaryWASMExpert Layer

The `TernaryWASMExpert` class (located in `qminiwasm/layers/ternary.py`) extends PyTorch's `nn.Linear` layer with ternary quantization:

```python
from qminiwasm.layers.ternary import TernaryWASMExpert

# Create a ternary quantized linear layer
layer = TernaryWASMExpert(in_features=4096, out_features=4096)
```

### Quantization Process

1. **Adaptive Thresholding**: Weights are normalized by their absolute mean
2. **Discretization**: Values rounded to nearest integer and clamped to `[-1, 1]`
3. **Straight-Through Estimator**: Gradients flow through continuous weights during backprop, while forward pass uses discrete ternary values

### Training with Ternary Quantization

```python
from qminiwasm.model import QMiniWASM
from qminiwasm.training.loop import run_training_loop

# Initialize model (includes TernaryWASMExpert layers)
model = QMiniWASM()

# Run training with ternary quantization
results = run_training_loop(
    epochs=10,
    batch_size=32,
    learning_rate=1e-4,
)
```

The training loop automatically applies ternary quantization to expert layers while maintaining gradient flow through the Straight-Through Estimator.

## Integration with WASM Execution

Ternary quantized weights are used when executing neural network operations within WASM modules. The deterministic nature of ternary values ensures consistent execution across different platforms.

## Configuration

Ternary quantization is enabled by default in `TernaryWASMExpert` layers. To disable quantization (use continuous weights), set `precision="float"`:

```python
layer = TernaryWASMExpert(4096, 4096, precision="float")
```

## Benefits

- **Reduced Model Size**: Ternary weights require fewer bits than float32
- **Deterministic Execution**: Discrete values ensure consistent behavior
- **Edge Deployment**: Lower memory footprint suitable for resource-constrained environments

## Limitations

- Quantization introduces approximation error compared to full-precision models
- Training may require more epochs to converge
- Some model architectures may not be suitable for ternary quantization
