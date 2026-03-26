"""Test script for WASM compilation and execution"""

import logging
import os
import random
import tempfile
import unittest

from qminiwasm.wasm_host import MESH_ALGORITHMS, MESH_EXPORT_NAMES, WasmEngine

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class TestWasmCompilation(unittest.TestCase):
    """Test cases for WASM compilation and execution"""

    def setUp(self):
        """Setup test environment"""
        self.wasm_engine = WasmEngine(use_mock=False)
        self.mesh_algorithms = ["hash", "encrypt", "network", "routing", "consensus"]

    def test_wasm_compilation(self):
        """Test WASM compilation for all algorithms"""
        logger.info("Testing WASM compilation for all algorithms...")
        for algo_name, c_code in MESH_ALGORITHMS.items():
            try:
                with tempfile.NamedTemporaryFile(suffix=".c", delete=False) as c_file:
                    c_file.write(c_code.encode())
                    c_path = c_file.name
                module = self.wasm_engine.compile_c_to_wasm(c_path)
                os.unlink(c_path)
                self.assertIsNotNone(module, f"Failed to compile {algo_name}")
                logger.info("Successfully compiled %s", algo_name)
            except Exception as e:
                self.fail(f"Compilation failed for {algo_name}: {str(e)}")

        logger.info("All algorithm compilation tests passed!")

    def test_wasm_execution(self):
        """Test WASM execution for all algorithms"""
        logger.info("Testing WASM execution for all algorithms...")
        for algo_name in self.mesh_algorithms:
            try:
                with tempfile.NamedTemporaryFile(suffix=".c", delete=False) as c_file:
                    c_file.write(MESH_ALGORITHMS[algo_name].encode())
                    c_path = c_file.name
                module = self.wasm_engine.compile_c_to_wasm(c_path)
                os.unlink(c_path)
                self.assertIsNotNone(module, f"Failed to compile {algo_name}")
                export = MESH_EXPORT_NAMES[algo_name]

                # Test execution with sample inputs
                for _ in range(3):
                    inputs = [random.randint(0, 1000), random.randint(0, 1000)]
                    output, hidden, target, _pre, _post = self.wasm_engine.execute_wasm(
                        module, export, inputs
                    )

                    self.assertIsNotNone(output, f"Execution failed for {algo_name}")
                    self.assertIsNotNone(hidden, f"Execution failed for {algo_name}")
                    self.assertIsNotNone(target, f"Execution failed for {algo_name}")
                    self.assertEqual(hidden.numel(), 4096)
                    self.assertEqual(target.numel(), 4096)

                    logger.info("Executed %s with inputs %s: output=%s", algo_name, inputs, output)

            except Exception as e:
                self.fail(f"Execution failed for {algo_name}: {str(e)}")

        logger.info("All algorithm execution tests passed!")

    def test_mock_fallback(self):
        """Test mock fallback when WASM compilation fails"""
        logger.info("Testing mock fallback...")
        # Create engine with mock enabled
        mock_engine = WasmEngine(use_mock=True)

        for algo_name in self.mesh_algorithms:
            try:
                # Test execution with mock
                for _ in range(3):
                    inputs = [random.randint(0, 1000), random.randint(0, 1000)]
                    output, hidden, target, _p, _q = mock_engine.execute_wasm(
                        None, algo_name, inputs
                    )

                    self.assertIsNotNone(output, f"Mock execution failed for {algo_name}")
                    self.assertIsNotNone(hidden, f"Mock execution failed for {algo_name}")
                    self.assertIsNotNone(target, f"Mock execution failed for {algo_name}")

                    logger.info(
                        "Mock executed %s with inputs %s: output=%s", algo_name, inputs, output
                    )

            except Exception as e:
                self.fail(f"Mock execution failed for {algo_name}: {str(e)}")

        logger.info("All mock execution tests passed!")

    def test_error_handling(self):
        """Test error handling in WASM engine"""
        logger.info("Testing error handling...")
        engine = WasmEngine(use_mock=False)

        # Test with invalid function name
        try:
            with tempfile.NamedTemporaryFile(suffix=".c", delete=False) as c_file:
                c_file.write(MESH_ALGORITHMS["hash"].encode())
                c_path = c_file.name
            module = engine.compile_c_to_wasm(c_path)
            os.unlink(c_path)
            self.assertIsNotNone(module)
            output, hidden, target, _pre, _post = engine.execute_wasm(
                module, "invalid_function", [1, 2]
            )
            self.assertEqual(output, 0, "Should return 0 for invalid function")
            self.assertIsNone(hidden, "Hidden state should be None for invalid function")
            self.assertIsNone(target, "Target state should be None for invalid function")
        except Exception as e:
            self.fail(f"Error handling test failed: {str(e)}")

        logger.info("Error handling tests passed!")


if __name__ == "__main__":
    unittest.main()
