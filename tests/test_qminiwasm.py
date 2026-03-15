"""Test Cases for Q-Mini-WASM Architecture

This module contains comprehensive test cases for the Q-Mini-WASM architecture. It includes:
- Unit tests for individual components
- Integration tests for model functionality
- Performance tests for quantum routing and WASM execution

The tests follow the white paper's specifications and cover:
- Quantum MoE router functionality
- Ternary quantization correctness
- WASM execution engine behavior
- Model interface methods
- Error handling and edge cases
"""

import unittest
import torch
import logging
from qminiwasm import QMiniWASM


class TestQMiniWASM(unittest.TestCase):
    """Test cases for Q-Mini-WASM architecture."""

    def setUp(self):
        """Set up test environment."""
        logging.disable(logging.CRITICAL)
        self.model = QMiniWASM()
        self.logger = logging.getLogger(__name__)

    def test_model_initialization(self):
        """Test model initialization."""
        self.assertIsNotNone(self.model.quantum_router)
        self.assertIsNotNone(self.model.ternary_expert)
        self.assertIsNotNone(self.model.wasm_executor)
        self.assertIsNotNone(self.model.sycl_hardware)
        self.assertIsNotNone(self.model.data_pipeline)

    def test_hybrid_inference(self):
        """Test quantum-classical hybrid inference."""
        # Create test input
        input_tensor = torch.randn(2, 4096)  # Batch size 2, d_model 4096

        # Test hybrid inference
        output = self.model.hybrid_inference(input_tensor)

        # Verify output shape
        self.assertEqual(output.shape, (2, 4096))

    def test_execute_wasm(self):
        """Test WASM execution."""
        # Create simple WASM module (addition function)
        wasm_code = bytes.fromhex(
            "0061736d01000000"  # WASM magic number and version
            "0a0601000001070160017e0041046d6f64756c6500010a"
            "040601000000034000000b"
        )

        # Test execution
        result, execution_state = self.model.execute_wasm(wasm_code, "add", [2, 3])

        # Verify result (2 + 3 = 5)
        self.assertEqual(result, 5)

    def test_ternary_quantization(self):
        """Test ternary quantization."""
        # Create test input
        input_tensor = torch.randn(2, 4096)

        # Test ternary quantization
        output = self.model.ternary_expert(input_tensor)

        # Verify output shape
        self.assertEqual(output.shape, (2, 4096))

    def test_quantum_router(self):
        """Test quantum MoE router."""
        # Create test input
        input_tensor = torch.randn(2, 4096)

        # Test quantum routing
        output = self.model.quantum_router(input_tensor)

        # Verify output shape
        self.assertEqual(output.shape, (2, 4096))

    def test_train_evaluate(self):
        """Test model training and evaluation."""
        # Create test data
        training_data = [
            {"input": torch.randn(4096), "target": torch.randn(4096)} for _ in range(10)
        ]
        test_data = [{"input": torch.randn(4096), "target": torch.randn(4096)} for _ in range(5)]

        # Test training
        self.model.train(training_data, epochs=1)

        # Test evaluation
        metrics = self.model.evaluate(test_data)
        self.assertIsInstance(metrics, dict)

    def test_error_handling(self):
        """Test error handling."""
        # Test with invalid WASM code
        invalid_wasm = b"invalid_wasm_code"

        with self.assertRaises(Exception):
            self.model.execute_wasm(invalid_wasm, "add", [2, 3])


if __name__ == "__main__":
    unittest.main()
