"""Test script for data pipeline components"""

import unittest
from qminiwasm.data.pipeline import DataPipeline
import logging

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class TestDataPipeline(unittest.TestCase):
    """Test cases for DataPipeline components"""

    def setUp(self):
        """Setup test environment"""
        self.pipeline = DataPipeline()
        self.mesh_algorithms = ["hash", "encrypt", "network", "routing", "consensus"]

    def test_generate_training_data(self):
        """Test training data generation"""
        logger.info("Testing generate_training_data...")
        data = self.pipeline.generate_training_data(
            algorithms=self.mesh_algorithms, num_samples=100
        )
        self.assertIsNotNone(data)
        self.assertGreater(len(data), 0)
        self.assertEqual(len(data), 500)  # 100 samples * 5 algorithms

        # Check data structure
        for sample in data:
            self.assertIn("algorithm", sample)
            self.assertIn("inputs", sample)
            self.assertIn("output", sample)
            self.assertIn("wasm_memory", sample)
            self.assertIn("stack_snapshot", sample)
            self.assertIn("execution_state", sample)

        logger.info("generate_training_data test passed!")

    def test_inject_faults(self):
        """Test fault injection"""
        logger.info("Testing inject_faults...")
        normal_data = self.pipeline.generate_training_data(algorithms=["hash"], num_samples=50)

        fault_types = ["bit_flip", "stack_drop", "out_of_bounds"]
        corrupted_data = self.pipeline.inject_faults(normal_data, fault_types)

        self.assertIsNotNone(corrupted_data)
        self.assertEqual(len(corrupted_data), len(normal_data))

        # Check that faults were injected
        fault_count = 0
        for sample in corrupted_data:
            if "faults" in sample:
                fault_count += 1
                self.assertIsInstance(sample["faults"], list)
                self.assertGreater(len(sample["faults"]), 0)

        self.assertGreater(fault_count, 0)
        logger.info("inject_faults test passed!")

    def test_state_recovery(self):
        """Test state recovery"""
        logger.info("Testing state_recovery...")
        corrupted_data = self.pipeline.generate_training_data(algorithms=["hash"], num_samples=50)

        # Inject recoverable faults
        corrupted_data = self.pipeline.inject_faults(
            corrupted_data, fault_types=["bit_flip", "stack_drop"]
        )

        recovered_data = self.pipeline.state_recovery(corrupted_data)

        self.assertIsNotNone(recovered_data)
        self.assertEqual(len(recovered_data), len(corrupted_data))

        # Check recovery information
        recovery_count = 0
        for sample in recovered_data:
            if "recovery" in sample:
                recovery_count += 1
                self.assertIsInstance(sample["recovery"], (str, list))

        self.assertGreater(recovery_count, 0)
        logger.info("state_recovery test passed!")

    def test_load_wasm_traces(self):
        """Test WASM traces loading"""
        logger.info("Testing load_wasm_traces...")
        traces = self.pipeline.load_wasm_traces(num_traces=100)

        self.assertIsNotNone(traces)
        self.assertEqual(len(traces), 100)

        for trace in traces:
            self.assertIn("linear_memory", trace)
            self.assertIn("stack_snapshot", trace)
            self.assertIn("instruction_pointer", trace)
            self.assertIn("algorithm", trace)

        logger.info("load_wasm_traces test passed!")

    def test_compress_deltas(self):
        """Test delta compression"""
        logger.info("Testing compress_deltas...")
        current = bytes([i % 256 for i in range(64)])
        baseline = bytes([0] * 64)

        from qminiwasm.data.pipeline import compress_deltas

        payload = compress_deltas(current, baseline)

        self.assertIsNotNone(payload)
        self.assertIsInstance(payload, dict)
        self.assertIn("format_version", payload)
        self.assertIn("length", payload)
        self.assertIn("checksum", payload)
        self.assertIn("deltas", payload)

        logger.info("compress_deltas test passed!")


if __name__ == "__main__":
    unittest.main()
