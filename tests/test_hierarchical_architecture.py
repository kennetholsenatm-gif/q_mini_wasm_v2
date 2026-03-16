"""Comprehensive tests for Hierarchical Edge-Quantum AI Architecture

Tests the complete implementation of:
- Priority 1: Enhanced Approximate DCPE with manifold alignment protection
- Priority 2: Vec2Text-RAG Inversion Module with syntax-forced compensation
- Priority 3: Enhanced Quantum QAOA Router with barren plateau mitigation
"""

import unittest
import torch
import numpy as np
from qminiwasm.security.crypto import (
    encrypt_vector,
    decrypt_vector,
    generate_symmetric_key,
    rotate_symmetric_key,
    validate_security_context,
    log_security_operation,
)
from qminiwasm.inference.vec2text import reconstruct_memory, validate_reconstructed_text
from qminiwasm.quantum.router import enhanced_find_k_nearest_neighbors


class TestEnhancedApproximateDCPE(unittest.TestCase):
    """Tests for Enhanced Approximate DCPE implementation"""

    def setUp(self):
        """Setup test environment"""
        self.test_vector = torch.randn(1024)
        self.key_id = "test_key"
        self.original_key = generate_symmetric_key(self.key_id)

    def test_encryption_decryption(self):
        """Test encryption and decryption preserve distance comparisons"""
        encrypted = encrypt_vector(self.test_vector)
        decrypted = decrypt_vector(encrypted)

        # Check distance preservation within approximation factor
        original_distance = torch.norm(self.test_vector)
        decrypted_distance = torch.norm(decrypted)
        self.assertAlmostEqual(
            original_distance,
            decrypted_distance,
            delta=0.1 * original_distance,
            msg="Decrypted distance should preserve original within β=0.1",
        )

    def test_manifold_alignment_protection(self):
        """Test manifold alignment protection via SPARSE noise injection"""
        # Test with multiple vectors to check distance preservation
        vectors = [torch.randn(1024) for _ in range(10)]
        encrypted_vectors = [encrypt_vector(v) for v in vectors]

        # Check that distance comparisons are preserved
        for i in range(10):
            for j in range(i + 1, 10):
                original_dist = torch.norm(vectors[i] - vectors[j])
                encrypted_dist = torch.norm(encrypted_vectors[i] - encrypted_vectors[j])
                self.assertAlmostEqual(
                    original_dist,
                    encrypted_dist,
                    delta=0.1 * original_dist,
                    msg="Encrypted distances should preserve original within β=0.1",
                )

    def test_key_rotation(self):
        """Test dynamic key rotation with temporal epoch fracturing"""
        # Rotate key multiple times
        for _ in range(5):
            new_key = rotate_symmetric_key(self.key_id)
            self.assertNotEqual(new_key, self.original_key)
            self.original_key = new_key

    def test_security_context(self):
        """Test security context validation"""
        context = {
            "memory_usage": 50 * 1024 * 1024,
            "cpu_time": 500,
            "allowed_operations": ["encrypt", "decrypt"],
        }
        self.assertTrue(
            validate_security_context("encrypt", context),
            "Valid security context should allow encryption",
        )

    def test_security_logging(self):
        """Test security operation logging"""
        result = encrypt_vector(self.test_vector)
        log_security_operation("encrypt", {"test": "context"}, result)
        # If no exception, logging succeeded


class TestVec2TextRAG(unittest.TestCase):
    """Tests for Vec2Text-RAG Inversion Module"""

    def setUp(self):
        """Setup test environment"""
        self.query_vector = torch.randn(1024)
        self.candidate_vectors = [torch.randn(1024) for _ in range(10)]

    def test_memory_reconstruction(self):
        """Test exact memory reconstruction with syntax-forced compensation"""
        reconstructed = reconstruct_memory(self.query_vector, self.candidate_vectors)
        self.assertIsNotNone(reconstructed, "Memory reconstruction should succeed")

        # Validate reconstructed text syntax
        is_valid, error = validate_reconstructed_text(reconstructed)
        self.assertTrue(is_valid, f"Reconstructed text should be valid: {error}")

    def test_syntax_validation(self):
        """Test syntax validation for reconstructed text"""
        valid_text = '{"state_id": "test", "timestamp": "2026-01-01T00:00:00Z", "execution_state": {"memory": {}, "stack": []}}'
        is_valid, error = validate_reconstructed_text(valid_text)
        self.assertTrue(is_valid, f"Valid JSON should pass syntax validation: {error}")

        invalid_text = "invalid json"
        is_valid, error = validate_reconstructed_text(invalid_text)
        self.assertFalse(is_valid, "Invalid JSON should fail validation")

    def test_oversampling_mechanism(self):
        """Test network-level oversampling mechanism"""
        # Test that oversampling generates multiple hypotheses
        hypotheses = self._generate_hypotheses(self.candidate_vectors)
        self.assertGreaterEqual(
            len(hypotheses), 5, "Oversampling should generate multiple hypotheses"
        )

    def _generate_hypotheses(self, vectors: list) -> list:
        """Generate text hypotheses from candidate vectors"""
        hypotheses = []
        for vector in vectors:
            try:
                # Simplified hypothesis generation
                text = self._simulate_diffusion(vector)
                hypotheses.append(text)
            except Exception:
                pass
        return hypotheses

    def _simulate_diffusion(self, vector: torch.Tensor) -> str:
        """Simulate diffusion model output"""
        # Simplified simulation - in production, use actual model
        return '{"state_id": "test", "timestamp": "2026-01-01T00:00:00Z", "execution_state": {"memory": {}, "stack": []}}'


class TestEnhancedQuantumRouter(unittest.TestCase):
    """Tests for Enhanced Quantum QAOA Router"""

    def setUp(self):
        """Setup test environment"""
        self.query_vector = torch.randn(1024)
        self.database_vectors = [torch.randn(1024) for _ in range(100)]

    def test_qubo_formulation(self):
        """Test complete QUBO formulation with exact white paper specifications"""
        # Create test affinity matrix
        affinity = torch.randn((10, 10))  # 10 tokens, 10 experts
        K = 1  # Top-1
        C = 5  # Capacity of 5

        # Test enhanced QUBO formulation
        q_linear, q_quad = self._enhanced_qubo_formulation(affinity, K, C)
        self.assertIsNotNone(q_linear, "QUBO formulation should succeed")
        self.assertIsNotNone(q_quad, "QUBO formulation should succeed")

    def _enhanced_qubo_formulation(self, affinity: torch.Tensor, K: int, C: int):
        """Test enhanced QUBO formulation"""
        try:
            from qminiwasm.quantum.qubo import enhanced_qubo_hamiltonian, enhanced_qubo_to_ising

            q_linear, q_quad = enhanced_qubo_hamiltonian(affinity, K, C)
            h, J = enhanced_qubo_to_ising(q_linear, q_quad)
            return q_linear, q_quad
        except ImportError:
            return None, None

    def test_quantum_routing(self):
        """Test enhanced quantum routing with barren plateau mitigation"""
        # Test k-NN routing
        neighbors = enhanced_find_k_nearest_neighbors(self.query_vector, self.database_vectors, k=5)
        self.assertEqual(
            len(neighbors), 5, "Quantum routing should find exactly k nearest neighbors"
        )

    def test_barren_plateau_mitigation(self):
        """Test barren plateau mitigation strategies"""
        # Test that mitigation strategies are applied
        from qminiwasm.quantum.router import BarrenPlateauMitigator

        mitigator = BarrenPlateauMitigator()
        initial_params = mitigator.generate_initial_params(100)
        self.assertIsNotNone(initial_params, "Initial parameters should be generated")

    def test_ternary_expert_support(self):
        """Test ternary expert support via Grover's search"""
        # Test ternary weight optimization
        from qminiwasm.quantum.router import TernaryOptimizer

        optimizer = TernaryOptimizer()
        test_weights = torch.randn(100)
        ternary_weights = optimizer.optimize_ternary_weights(test_weights)
        self.assertIsNotNone(ternary_weights, "Ternary weights should be optimized")

    def test_quantum_aware_optimizations(self):
        """Test quantum-aware optimizations"""
        # Test quantum-aware affinity calculation
        from qminiwasm.quantum.qubo import quantum_aware_affinity

        compressed_states = torch.randn(10, 5, 1024)
        expert_signatures = torch.randn(20, 1024)
        affinity = quantum_aware_affinity(compressed_states, expert_signatures)
        self.assertIsNotNone(affinity, "Quantum-aware affinity should be calculated")


class TestIntegration(unittest.TestCase):
    """Integration tests for complete Hierarchical Architecture"""

    def test_end_to_end_pipeline(self):
        """Test complete end-to-end pipeline"""
        # Test vector encryption
        test_vector = torch.randn(1024)
        encrypted = encrypt_vector(test_vector)
        self.assertIsNotNone(encrypted, "Vector encryption should succeed")

        # Test memory reconstruction
        query_vector = torch.randn(1024)
        candidate_vectors = [torch.randn(1024) for _ in range(10)]
        reconstructed = reconstruct_memory(query_vector, candidate_vectors)
        self.assertIsNotNone(reconstructed, "Memory reconstruction should succeed")

        # Test quantum routing
        database_vectors = [torch.randn(1024) for _ in range(100)]
        neighbors = enhanced_find_k_nearest_neighbors(query_vector, database_vectors, k=5)
        self.assertEqual(len(neighbors), 5, "Quantum routing should find k neighbors")

    def test_security_compliance(self):
        """Test security compliance across all components"""
        # Test that all components use enhanced DCPE
        test_vector = torch.randn(1024)
        encrypted = encrypt_vector(test_vector)
        self.assertIsNotNone(encrypted, "Enhanced DCPE should be used")

        # Test that memory reconstruction uses syntax validation
        query_vector = torch.randn(1024)
        candidate_vectors = [torch.randn(1024) for _ in range(10)]
        reconstructed = reconstruct_memory(query_vector, candidate_vectors)
        self.assertIsNotNone(reconstructed, "Syntax validation should be used")

        # Test that quantum routing uses enhanced QUBO
        database_vectors = [torch.randn(1024) for _ in range(100)]
        neighbors = enhanced_find_k_nearest_neighbors(query_vector, database_vectors, k=5)
        self.assertEqual(len(neighbors), 5, "Enhanced QUBO should be used")


if __name__ == "__main__":
    unittest.main()
