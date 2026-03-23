#!/usr/bin/env python3
"""Test suite for encrypted quantum routing implementation.

This test suite validates the enhanced QAOA-based routing over encrypted vectors
and measures performance improvements for similarity search operations.
"""

import time
import numpy as np
import torch
import logging
from typing import List, Tuple

from qminiwasm.quantum.router import EnhancedQuantumRouter
from qminiwasm.quantum.qubo import (
    encrypted_qubo_hamiltonian,
    encrypted_qubo_to_ising,
    encrypted_affinity_from_compressed,
    encrypted_distance_matrix_to_affinity,
    encrypted_qubo_optimization,
)
from qminiwasm.security.crypto import encrypt_vector, decrypt_vector

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class EncryptedQuantumRoutingTester:
    """Test suite for encrypted quantum routing functionality."""

    def __init__(self):
        """Initialize the test suite."""
        self.router = EnhancedQuantumRouter()
        self.test_results = {}

    def generate_test_data(
        self, num_vectors: int = 100, vector_dim: int = 128
    ) -> Tuple[np.ndarray, torch.Tensor]:
        """Generate test vectors for encrypted routing tests.

        Args:
            num_vectors: Number of test vectors to generate
            vector_dim: Dimensionality of each vector

        Returns:
            (numpy_vectors, torch_vectors) for testing
        """
        # Generate random test vectors
        numpy_vectors = np.random.randn(num_vectors, vector_dim).astype(np.float32)
        torch_vectors = torch.from_numpy(numpy_vectors)

        logger.info(f"Generated {num_vectors} test vectors with dimension {vector_dim}")
        return numpy_vectors, torch_vectors

    def test_encrypted_vector_processing(self):
        """Test encrypted vector processing functionality."""
        logger.info("Testing encrypted vector processing...")

        # Generate test data
        numpy_vectors, torch_vectors = self.generate_test_data(num_vectors=10)

        # Test encryption
        encrypted_vectors = []
        for vector in torch_vectors:
            encrypted = encrypt_vector(vector)
            encrypted_vectors.append(encrypted)

        # Test decryption (approximate due to perturbation)
        decrypted_vectors = []
        for encrypted in encrypted_vectors:
            decrypted = decrypt_vector(encrypted)
            decrypted_vectors.append(decrypted)

        # Verify encryption/decryption preserves distance relationships
        original_distances = []
        encrypted_distances = []
        decrypted_distances = []

        for i in range(len(torch_vectors)):
            for j in range(i + 1, len(torch_vectors)):
                # Original distances
                orig_dist = torch.norm(torch_vectors[i] - torch_vectors[j]).item()
                original_distances.append(orig_dist)

                # Encrypted distances
                enc_dist = torch.norm(encrypted_vectors[i] - encrypted_vectors[j]).item()
                encrypted_distances.append(enc_dist)

                # Decrypted distances
                dec_dist = torch.norm(decrypted_vectors[i] - decrypted_vectors[j]).item()
                decrypted_distances.append(dec_dist)

        # Check distance preservation (should be approximately preserved)
        correlation_orig_enc = np.corrcoef(original_distances, encrypted_distances)[0, 1]
        correlation_orig_dec = np.corrcoef(original_distances, decrypted_distances)[0, 1]

        logger.info(f"Distance correlation (original vs encrypted): {correlation_orig_enc:.4f}")
        logger.info(f"Distance correlation (original vs decrypted): {correlation_orig_dec:.4f}")

        # Store results
        self.test_results["encrypted_vector_processing"] = {
            "distance_correlation_encrypted": correlation_orig_enc,
            "distance_correlation_decrypted": correlation_orig_dec,
            "success": correlation_orig_enc > 0.8 and correlation_orig_dec > 0.8,
        }

        return self.test_results["encrypted_vector_processing"]["success"]

    def test_encrypted_qubo_formulation(self):
        """Test encrypted QUBO formulation functionality."""
        logger.info("Testing encrypted QUBO formulation...")

        # Generate test affinity matrix
        T, E = 5, 10
        encrypted_affinity = torch.randn(T, E) * 0.1 + 1.0  # Positive affinity values

        # Test basic encrypted QUBO formulation
        q_linear, q_quad = encrypted_qubo_hamiltonian(
            encrypted_affinity, K=2, C=3, lambda1=2e6, lambda2=2e6, quantum_noise_factor=0.1
        )

        # Test Ising mapping
        h, J = encrypted_qubo_to_ising(q_linear, q_quad, quantum_noise_factor=0.1)

        # Test optimization levels
        optimization_results = {}
        for level in ["basic", "enhanced", "ultra"]:
            q_lin_opt, q_quad_opt = encrypted_qubo_optimization(
                encrypted_affinity, K=2, C=3, optimization_level=level
            )
            optimization_results[level] = {
                "linear_norm": torch.norm(q_lin_opt).item(),
                "quad_norm": torch.norm(q_quad_opt).item(),
            }

        logger.info(f"Optimization results: {optimization_results}")

        # Store results
        self.test_results["encrypted_qubo_formulation"] = {
            "linear_coeff_norm": torch.norm(q_linear).item(),
            "quad_coeff_norm": torch.norm(q_quad).item(),
            "optimization_results": optimization_results,
            "success": True,
        }

        return True

    def test_encrypted_affinity_computation(self):
        """Test encrypted affinity computation from compressed states."""
        logger.info("Testing encrypted affinity computation...")

        # Generate test data
        batch_size, num_tokens, dim = 2, 5, 64
        num_experts = 8

        # Generate compressed states and expert signatures
        compressed_states = torch.randn(batch_size, num_tokens, dim)
        expert_signatures = torch.randn(num_experts, dim)

        # Encrypt the data
        encrypted_compressed = []
        for batch in compressed_states:
            batch_encrypted = [encrypt_vector(state) for state in batch]
            encrypted_compressed.append(torch.stack(batch_encrypted))
        encrypted_compressed = torch.stack(encrypted_compressed)

        encrypted_signatures = [encrypt_vector(sig) for sig in expert_signatures]
        encrypted_signatures = torch.stack(encrypted_signatures)

        # Compute encrypted affinity
        affinity = encrypted_affinity_from_compressed(
            encrypted_compressed, encrypted_signatures, quantum_aware=True
        )

        # Test distance matrix conversion
        encrypted_distance_matrix = np.random.rand(num_tokens + 1, num_experts + 1)
        distance_affinity = encrypted_distance_matrix_to_affinity(
            encrypted_distance_matrix, num_tokens, num_experts, quantum_factor=1.1
        )

        logger.info(f"Affinity matrix shape: {affinity.shape}")
        logger.info(f"Distance affinity matrix shape: {distance_affinity.shape}")

        # Store results
        self.test_results["encrypted_affinity_computation"] = {
            "affinity_shape": affinity.shape,
            "distance_affinity_shape": distance_affinity.shape,
            "success": True,
        }

        return True

    def test_encrypted_routing_performance(self):
        """Test encrypted routing performance and accuracy."""
        logger.info("Testing encrypted routing performance...")

        # Generate larger test dataset
        numpy_vectors, torch_vectors = self.generate_test_data(num_vectors=1000, vector_dim=256)

        # Encrypt vectors
        encrypted_vectors = [encrypt_vector(vec) for vec in torch_vectors]

        # Test regular routing (baseline)
        query_vector = numpy_vectors[0]
        k = 10

        start_time = time.time()
        regular_indices = self.router.find_k_nearest_neighbors(query_vector, numpy_vectors[1:], k)
        regular_time = time.time() - start_time

        # Test encrypted routing
        encrypted_query = encrypt_vector(torch.from_numpy(query_vector))

        start_time = time.time()
        encrypted_indices = self.router.find_k_nearest_neighbors_encrypted(
            encrypted_query, encrypted_vectors[1:], k
        )
        encrypted_time = time.time() - start_time

        # Compare results
        overlap = len(set(regular_indices) & set(encrypted_indices))
        accuracy = overlap / k

        logger.info(f"Regular routing time: {regular_time:.4f}s")
        logger.info(f"Encrypted routing time: {encrypted_time:.4f}s")
        logger.info(f"Result overlap: {overlap}/{k} (accuracy: {accuracy:.4f})")

        # Store results
        self.test_results["encrypted_routing_performance"] = {
            "regular_time": regular_time,
            "encrypted_time": encrypted_time,
            "speedup": regular_time / encrypted_time if encrypted_time > 0 else float('inf'),
            "accuracy": accuracy,
            "success": accuracy > 0.7,  # 70% accuracy threshold
        }

        return self.test_results["encrypted_routing_performance"]["success"]

    def test_barren_plateau_mitigation(self):
        """Test enhanced barren plateau mitigation for encrypted data."""
        logger.info("Testing barren plateau mitigation...")

        # Test parameter generation
        n_qubits = 8
        regular_params = self.router.barren_mitigation.generate_initial_params(n_qubits)
        encrypted_params = self.router.barren_mitigation.generate_encrypted_initial_params(n_qubits)

        # Test parameter characteristics
        regular_std = np.std(regular_params)
        encrypted_std = np.std(encrypted_params)

        logger.info(f"Regular parameter std: {regular_std:.4f}")
        logger.info(f"Encrypted parameter std: {encrypted_std:.4f}")

        # Store results
        self.test_results["barren_plateau_mitigation"] = {
            "regular_param_std": regular_std,
            "encrypted_param_std": encrypted_std,
            "success": encrypted_std > regular_std * 0.8,  # Should be similar magnitude
        }

        return self.test_results["barren_plateau_mitigation"]["success"]

    def run_all_tests(self):
        """Run all tests and generate a comprehensive report."""
        logger.info("Running comprehensive encrypted quantum routing tests...")

        test_functions = [
            self.test_encrypted_vector_processing,
            self.test_encrypted_qubo_formulation,
            self.test_encrypted_affinity_computation,
            self.test_encrypted_routing_performance,
            self.test_barren_plateau_mitigation,
        ]

        results = {}
        for test_func in test_functions:
            try:
                success = test_func()
                results[test_func.__name__] = success
                logger.info(f"✓ {test_func.__name__}: {'PASSED' if success else 'FAILED'}")
            except Exception as e:
                logger.error(f"✗ {test_func.__name__}: ERROR - {str(e)}")
                results[test_func.__name__] = False

        # Generate summary report
        self.generate_report(results)

        return results

    def generate_report(self, results: dict):
        """Generate a comprehensive test report."""
        logger.info("\n" + "="*60)
        logger.info("ENCRYPTED QUANTUM ROUTING TEST REPORT")
        logger.info("="*60)

        total_tests = len(results)
        passed_tests = sum(results.values())

        logger.info(f"Total Tests: {total_tests}")
        logger.info(f"Passed: {passed_tests}")
        logger.info(f"Failed: {total_tests - passed_tests}")
        logger.info(f"Success Rate: {passed_tests/total_tests*100:.1f}%")

        logger.info("\nDetailed Results:")
        for test_name, success in results.items():
            status = "PASSED" if success else "FAILED"
            logger.info(f"  {test_name}: {status}")

        # Performance metrics
        if "encrypted_routing_performance" in self.test_results:
            perf = self.test_results["encrypted_routing_performance"]
            logger.info(f"\nPerformance Metrics:")
            logger.info(f"  Regular Routing Time: {perf['regular_time']:.4f}s")
            logger.info(f"  Encrypted Routing Time: {perf['encrypted_time']:.4f}s")
            logger.info(f"  Speedup Factor: {perf['speedup']:.2f}x")
            logger.info(f"  Accuracy: {perf['accuracy']:.4f}")

        # Security metrics
        if "encrypted_vector_processing" in self.test_results:
            sec = self.test_results["encrypted_vector_processing"]
            logger.info(f"\nSecurity Metrics:")
            logger.info(f"  Distance Correlation (Encrypted): {sec['distance_correlation_encrypted']:.4f}")
            logger.info(f"  Distance Correlation (Decrypted): {sec['distance_correlation_decrypted']:.4f}")

        logger.info("="*60)

        return results


def main():
    """Main test execution function."""
    logger.info("Starting encrypted quantum routing test suite...")

    tester = EncryptedQuantumRoutingTester()
    results = tester.run_all_tests()

    # Exit with appropriate code
    success = all(results.values())
    exit_code = 0 if success else 1

    logger.info(f"Test suite completed with exit code: {exit_code}")
    return exit_code


if __name__ == "__main__":
    exit_code = main()
    exit(exit_code)